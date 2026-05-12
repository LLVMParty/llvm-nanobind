"""Submission evaluator for the workshop portal POC."""

from __future__ import annotations

import argparse
import inspect
import io
import json
import re
import traceback
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import Any

import llvm

try:
    from .exercise_bank import get_exercise
except ImportError:
    from exercise_bank import get_exercise


def _result_preview(value: Any, limit: int = 400) -> str:
    if isinstance(value, str):
        text = value
    elif isinstance(value, (int, float, bool, type(None))):
        text = repr(value)
    else:
        # LLVM wrapper objects may become invalid by the time we preview them.
        # Avoid calling repr() on arbitrary objects here.
        text = f"<{type(value).__name__}>"

    if len(text) <= limit:
        return text
    return text[:limit] + "\n...<truncated>"


def _normalize_ir(ir_text: str) -> str:
    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir_text) as mod:
            return mod.to_string()


def _parse_verified_module(ir_text: str) -> tuple[bool, str]:
    try:
        with llvm.create_context() as ctx:
            with ctx.parse_ir(ir_text) as mod:
                if mod.verify():
                    return True, ""
                return False, mod.verification_error
    except BaseException as exc:
        return False, f"Parse/verify error: {type(exc).__name__}: {exc}"


def _invoke_entrypoint(func: Any, input_ir: str, name: str) -> Any:
    sig = inspect.signature(func)
    params = list(sig.parameters.values())
    positional = [
        p
        for p in params
        if p.kind in (inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD)
    ]
    has_varargs = any(p.kind == inspect.Parameter.VAR_POSITIONAL for p in params)

    if has_varargs:
        return func(input_ir)
    if len(positional) == 0:
        return func()
    if len(positional) == 1:
        return func(input_ir)
    raise RuntimeError(f"Entrypoint `{name}` must accept 0 or 1 positional argument.")


def _run_user_code(submission_code: str, input_ir: str, submission_mode: str) -> dict[str, Any]:
    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()
    namespace: dict[str, Any] = {
        "__name__": "__submission__",
        "input_ir": input_ir,
    }

    try:
        with redirect_stdout(stdout_buf), redirect_stderr(stderr_buf):
            exec(submission_code, namespace)
    except BaseException as exc:
        return {
            "result": None,
            "exc": exc,
            "stdout": stdout_buf.getvalue(),
            "stderr": stderr_buf.getvalue(),
            "entrypoint": "exec",
        }

    solve = namespace.get("solve")
    if callable(solve):
        try:
            with redirect_stdout(stdout_buf), redirect_stderr(stderr_buf):
                result = _invoke_entrypoint(solve, input_ir, "solve")
            return {
                "result": result,
                "exc": None,
                "stdout": stdout_buf.getvalue(),
                "stderr": stderr_buf.getvalue(),
                "entrypoint": "solve",
            }
        except BaseException as exc:
            return {
                "result": None,
                "exc": exc,
                "stdout": stdout_buf.getvalue(),
                "stderr": stderr_buf.getvalue(),
                "entrypoint": "solve",
            }

    main = namespace.get("main")
    if callable(main):
        try:
            with redirect_stdout(stdout_buf), redirect_stderr(stderr_buf):
                result = _invoke_entrypoint(main, input_ir, "main")
            return {
                "result": result,
                "exc": None,
                "stdout": stdout_buf.getvalue(),
                "stderr": stderr_buf.getvalue(),
                "entrypoint": "main",
            }
        except BaseException as exc:
            return {
                "result": None,
                "exc": exc,
                "stdout": stdout_buf.getvalue(),
                "stderr": stderr_buf.getvalue(),
                "entrypoint": "main",
            }

    if submission_mode == "function":
        return {
            "result": None,
            "exc": RuntimeError("Submission must define callable `solve(input_ir: str)`."),
            "stdout": stdout_buf.getvalue(),
            "stderr": stderr_buf.getvalue(),
            "entrypoint": "none",
        }

    # script/top-level mode: successful execution can be validated via stdout/stderr.
    return {
        "result": None,
        "exc": None,
        "stdout": stdout_buf.getvalue(),
        "stderr": stderr_buf.getvalue(),
        "entrypoint": "top_level",
    }


def _extract_ir_from_text(text: str) -> str | None:
    raw = text.strip()
    if not raw:
        return None

    candidates = [raw]
    for marker in ("; ModuleID", "source_filename"):
        idx = raw.find(marker)
        if idx > 0:
            candidates.append(raw[idx:])

    for candidate in candidates:
        ok, _ = _parse_verified_module(candidate)
        if ok:
            return candidate
    return None


def _require_ir_result(
    run: dict[str, Any],
    exercise: dict[str, Any] | None = None,
    allow_stdout_ir: bool | None = None,
) -> tuple[bool, str, str]:
    result = run.get("result")
    exc = run.get("exc")
    stdout_text = str(run.get("stdout", ""))

    if allow_stdout_ir is None and exercise is not None:
        allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    if allow_stdout_ir is None:
        allow_stdout_ir = False

    if exc is not None:
        return False, "", f"Submission raised {type(exc).__name__}: {exc}"

    candidate_ir: str | None = None
    if isinstance(result, str):
        candidate_ir = result
    elif allow_stdout_ir:
        candidate_ir = _extract_ir_from_text(stdout_text)
        if candidate_ir is None:
            return (
                False,
                "",
                "Expected IR text (returned str or printed module). "
                f"Got return type {type(result).__name__} and no parseable IR in stdout.",
            )
    else:
        return False, "", f"Expected `solve()` to return str IR, got {type(result).__name__}"

    ok, reason = _parse_verified_module(candidate_ir)
    if not ok:
        return False, "", f"Returned IR is not valid: {reason}"
    try:
        normalized = _normalize_ir(candidate_ir)
    except BaseException as e:
        return False, "", f"Could not normalize returned IR: {type(e).__name__}: {e}"
    return True, normalized, ""


def _combined_text(run: dict[str, Any]) -> str:
    parts = []
    result = run.get("result")
    if isinstance(result, str):
        parts.append(result)
    stdout_text = str(run.get("stdout", ""))
    stderr_text = str(run.get("stderr", ""))
    if stdout_text:
        parts.append(stdout_text)
    if stderr_text:
        parts.append(stderr_text)
    return "\n".join(parts)


def _validate_f01(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    expected = _normalize_ir(str(exercise["input_ir"]))
    if normalized != expected:
        return False, "Output does not match canonical round-trip of the input module."
    return True, "Pass: parsed and round-tripped input IR correctly."


def _validate_f02(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            fn = mod.get_function("sum2")
            if fn is None:
                return False, "Missing function declaration `@sum2`."
            if not fn.is_declaration:
                return False, "`@sum2` should be a declaration (no body)."
            if fn.param_count != 2:
                return False, "`@sum2` must have exactly 2 parameters."
            p0 = fn.get_param(0)
            p1 = fn.get_param(1)
            if p0.type.kind != llvm.TypeKind.Integer or p0.type.int_width != 32:
                return False, "Parameter 0 must be i32."
            if p1.type.kind != llvm.TypeKind.Integer or p1.type.int_width != 32:
                return False, "Parameter 1 must be i32."
    if "declare i32 @sum2(i32, i32)" not in normalized:
        return False, "Return type/signature text does not match `declare i32 @sum2(i32, i32)`."
    return True, "Pass: added correct declaration `sum2(i32, i32) -> i32`."


def _validate_f03(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            fn = mod.get_function("answer")
            if fn is None:
                return False, "Missing function `@answer`."
            if fn.is_declaration:
                return False, "`@answer` must have a body."
            if fn.param_count != 0:
                return False, "`@answer` should take no parameters."
            term = fn.entry_block.terminator
            if term.opcode != llvm.Opcode.Ret:
                return False, "Entry block terminator must be `ret`."
            if term.num_operands != 1:
                return False, "Return must have exactly one operand."
            rv = term.get_operand(0)
            if not rv.is_constant_int or rv.const_sext_value != 42:
                return False, "Return value must be constant `i32 42`."
    return True, "Pass: built `@answer` returning 42."


def _validate_f04(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            g32 = mod.get_global("g_i32")
            g64 = mod.get_global("g_i64")
            if g32 is None or g64 is None:
                return False, "Expected globals `@g_i32` and `@g_i64`."
            if g32.initializer is None or g64.initializer is None:
                return False, "Both globals must have initializers."
            if not g32.initializer.is_constant_int or g32.initializer.const_sext_value != 7:
                return False, "`@g_i32` initializer must be integer 7."
            if not g64.initializer.is_constant_int or g64.initializer.const_sext_value != 7:
                return False, "`@g_i64` initializer must be integer 7."
            if g32.initializer.type.int_width != 32:
                return False, "`@g_i32` initializer must be i32."
            if g64.initializer.type.int_width != 64:
                return False, "`@g_i64` initializer must be i64."
    return True, "Pass: created typed integer globals correctly."


def _validate_f08(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            counter = mod.get_global("counter")
            if counter is None:
                return False, "Missing global `@counter`."
            if counter.linkage != llvm.Linkage.Internal:
                return False, "`@counter` must have internal linkage."
            if counter.initializer is None:
                return False, "`@counter` must have initializer `i32 0`."
            if not counter.initializer.is_constant_int or counter.initializer.const_sext_value != 0:
                return False, "`@counter` initializer must be integer 0."
            if counter.initializer.type.int_width != 32:
                return False, "`@counter` initializer type must be i32."
    return True, "Pass: added internal i32 counter global."


def _validate_b01(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            fn = mod.get_function("arith")
            if fn is None:
                return False, "Missing function `@arith`."
            if fn.is_declaration:
                return False, "`@arith` must have a function body."
            if fn.param_count != 3:
                return False, "`@arith` must take three i32 parameters."
            insts = fn.entry_block.instructions
            if not insts:
                return False, "`@arith` entry block has no instructions."
            if insts[-1].opcode != llvm.Opcode.Ret:
                return False, "`@arith` must end with `ret`."
            add_insts = [i for i in insts if i.opcode == llvm.Opcode.Add]
            mul_insts = [i for i in insts if i.opcode == llvm.Opcode.Mul]
            if not add_insts:
                return False, "Expected at least one integer `add` instruction."
            if not mul_insts:
                return False, "Expected at least one integer `mul` instruction."
            ret_op = insts[-1].get_operand(0)
            if not any(ret_op == mul for mul in mul_insts):
                return False, "Return operand should be the `mul` result."
            add_feeds_mul = False
            for mul in mul_insts:
                for add in add_insts:
                    for idx in range(mul.num_operands):
                        if mul.get_operand(idx) == add:
                            add_feeds_mul = True
                            break
                    if add_feeds_mul:
                        break
                if add_feeds_mul:
                    break
            if not add_feeds_mul:
                return False, "Expected multiplication to consume an addition result."
    return True, "Pass: built arithmetic chain `(a + b) * c`."


def _validate_b02(run: dict[str, Any], exercise: dict[str, Any]) -> tuple[bool, str]:
    allow_stdout_ir = bool(exercise.get("allow_stdout_ir", False))
    ok, normalized, err = _require_ir_result(run, allow_stdout_ir=allow_stdout_ir)
    if not ok:
        return False, err
    with llvm.create_context() as ctx:
        with ctx.parse_ir(normalized) as mod:
            fn = mod.get_function("mem_roundtrip")
            if fn is None:
                return False, "Missing function `@mem_roundtrip`."
            if fn.is_declaration:
                return False, "`@mem_roundtrip` must have a function body."
            if fn.param_count != 1:
                return False, "`@mem_roundtrip` must take exactly one i32 parameter."

            insts = fn.entry_block.instructions
            if len(insts) < 4:
                return False, "Expected at least alloca/store/load/ret in entry block."

            ops = [inst.opcode for inst in insts]
            required = [llvm.Opcode.Alloca, llvm.Opcode.Store, llvm.Opcode.Load, llvm.Opcode.Ret]
            cursor = 0
            for opcode in ops:
                if opcode == required[cursor]:
                    cursor += 1
                    if cursor == len(required):
                        break
            if cursor != len(required):
                return False, "Instruction order must include alloca -> store -> load -> ret."

            load_insts = [inst for inst in insts if inst.opcode == llvm.Opcode.Load]
            ret_inst = insts[-1]
            if ret_inst.opcode != llvm.Opcode.Ret:
                return False, "Function must end with `ret`."
            ret_val = ret_inst.get_operand(0)
            if not any(ret_val == load for load in load_insts):
                return False, "Return value should come from a `load` result."
    return True, "Pass: built memory round-trip with alloca/store/load/ret."


def _to_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return str(value)


def _check_text_contains(haystack: str, needle: str, case_insensitive: bool) -> bool:
    if case_insensitive:
        return needle.lower() in haystack.lower()
    return needle in haystack


def _check_text_regex(haystack: str, pattern: str, case_insensitive: bool) -> bool:
    flags = re.IGNORECASE if case_insensitive else 0
    return re.search(pattern, haystack, flags=flags) is not None


def _check_source_text(source: str, run: dict[str, Any]) -> str:
    if source == "stdout":
        return _to_text(run.get("stdout"))
    if source == "stderr":
        return _to_text(run.get("stderr"))
    if source == "result":
        return _to_text(run.get("result"))
    return _combined_text(run)


def _evaluate_check(check: dict[str, Any], run: dict[str, Any]) -> tuple[bool, str]:
    ctype = check.get("type")
    exc = run.get("exc")
    entrypoint = _to_text(run.get("entrypoint"))
    result = run.get("result")

    if ctype == "no_exception":
        if exc is None:
            return True, "no_exception"
        return False, f"Expected no exception, got {type(exc).__name__}: {exc}"

    if ctype == "exception_type_is":
        expected = _to_text(check.get("name"))
        actual = type(exc).__name__ if exc is not None else "None"
        if exc is not None and actual == expected:
            return True, f"exception_type_is:{expected}"
        return False, f"Expected exception type {expected}, got {actual}"

    if ctype == "entrypoint_in":
        allowed = check.get("allowed", [])
        if isinstance(allowed, list) and entrypoint in [str(x) for x in allowed]:
            return True, f"entrypoint_in:{entrypoint}"
        return False, f"Entrypoint `{entrypoint}` is not in allowed set {allowed}"

    if ctype == "result_type_is":
        expected = _to_text(check.get("name")).lower()
        actual = type(result).__name__.lower()
        type_aliases = {
            "none": "nonetype",
            "str": "str",
            "int": "int",
            "float": "float",
            "bool": "bool",
        }
        expected_norm = type_aliases.get(expected, expected)
        if actual == expected_norm:
            return True, f"result_type_is:{expected}"
        return False, f"Expected result type {expected}, got {type(result).__name__}"

    if ctype in ("stdout_contains", "stderr_contains", "result_contains", "combined_contains"):
        source_map = {
            "stdout_contains": "stdout",
            "stderr_contains": "stderr",
            "result_contains": "result",
            "combined_contains": "combined",
        }
        source = source_map[ctype]
        text = _check_source_text(source, run)
        needle = _to_text(check.get("text"))
        ci = bool(check.get("case_insensitive", False))
        if _check_text_contains(text, needle, ci):
            return True, f"{ctype}:{needle}"
        return False, f"Expected {source} to contain `{needle}`"

    if ctype in ("stdout_regex", "stderr_regex", "result_regex", "combined_regex"):
        source_map = {
            "stdout_regex": "stdout",
            "stderr_regex": "stderr",
            "result_regex": "result",
            "combined_regex": "combined",
        }
        source = source_map[ctype]
        text = _check_source_text(source, run)
        pattern = _to_text(check.get("pattern"))
        ci = bool(check.get("case_insensitive", False))
        if _check_text_regex(text, pattern, ci):
            return True, f"{ctype}:{pattern}"
        return False, f"Expected {source} to match regex `{pattern}`"

    if ctype == "any_of":
        nested = check.get("checks", [])
        if not isinstance(nested, list) or not nested:
            return False, "any_of requires non-empty `checks` list"
        failures: list[str] = []
        for child in nested:
            if not isinstance(child, dict):
                failures.append("invalid nested check")
                continue
            ok, msg = _evaluate_check(child, run)
            if ok:
                return True, "any_of"
            failures.append(msg)
        return False, "None of any_of checks passed: " + "; ".join(failures)

    if ctype == "all_of":
        nested = check.get("checks", [])
        if not isinstance(nested, list) or not nested:
            return False, "all_of requires non-empty `checks` list"
        failures: list[str] = []
        for child in nested:
            if not isinstance(child, dict):
                failures.append("invalid nested check")
                continue
            ok, msg = _evaluate_check(child, run)
            if not ok:
                failures.append(msg)
        if failures:
            return False, "all_of failed: " + "; ".join(failures)
        return True, "all_of"

    return False, f"Unsupported check type `{ctype}`"


def _run_metadata_checks(exercise: dict[str, Any], run: dict[str, Any]) -> tuple[bool, str]:
    validation = exercise.get("validation", {})
    if not isinstance(validation, dict):
        return False, "Exercise `validation` must be a mapping/object."
    checks = validation.get("checks", [])
    if checks is None:
        checks = []
    if not isinstance(checks, list):
        return False, "Exercise `validation.checks` must be a list."
    failures: list[str] = []
    for i, check in enumerate(checks):
        if not isinstance(check, dict):
            failures.append(f"check[{i}] is not an object")
            continue
        ok, msg = _evaluate_check(check, run)
        if not ok:
            failures.append(f"check[{i}] {msg}")
    if failures:
        return False, "Metadata checks failed:\n- " + "\n- ".join(failures)
    return True, "Metadata checks passed."


_FILE_VALIDATOR_CACHE: dict[str, Any] = {}


def _validator_helpers() -> dict[str, Any]:
    return {
        "llvm": llvm,
        "require_ir_result": _require_ir_result,
        "normalize_ir": _normalize_ir,
        "parse_verified_module": _parse_verified_module,
        "combined_text": _combined_text,
    }


def _load_custom_validator_from_file(exercise: dict[str, Any], validator_file: str) -> Any:
    base_dir_raw = exercise.get("_base_dir")
    if not isinstance(base_dir_raw, str) or not base_dir_raw:
        raise RuntimeError("Exercise is missing `_base_dir`; cannot resolve custom validator file.")
    validator_path = (Path(base_dir_raw) / validator_file).resolve()
    key = str(validator_path)
    if key in _FILE_VALIDATOR_CACHE:
        return _FILE_VALIDATOR_CACHE[key]

    if not validator_path.exists():
        raise FileNotFoundError(f"Custom validator file not found: {validator_path}")
    code = validator_path.read_text(encoding="utf-8")
    namespace: dict[str, Any] = {}
    exec(code, namespace)
    validate = namespace.get("validate")
    if not callable(validate):
        raise RuntimeError(
            f"Custom validator file {validator_path} must define callable `validate`."
        )
    _FILE_VALIDATOR_CACHE[key] = validate
    return validate


def _infer_custom_validator_file(exercise: dict[str, Any]) -> str | None:
    base_dir_raw = exercise.get("_base_dir")
    stem_raw = exercise.get("_stem")
    if not isinstance(base_dir_raw, str) or not isinstance(stem_raw, str):
        return None
    candidate = Path(base_dir_raw) / f"{stem_raw}.validator.py"
    if candidate.exists():
        return candidate.name
    return None


def _invoke_custom_validator(
    validator: Any,
    run: dict[str, Any],
    exercise: dict[str, Any],
    helpers: dict[str, Any],
) -> tuple[bool, str]:
    sig = inspect.signature(validator)
    positional = [
        p
        for p in sig.parameters.values()
        if p.kind in (inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD)
    ]
    if len(positional) <= 2:
        return validator(run, exercise)
    return validator(run, exercise, helpers)


def evaluate(exercise_id: str, submission_code: str) -> dict[str, Any]:
    try:
        exercise = get_exercise(exercise_id, include_solution=True)
    except KeyError as exc:
        return {
            "passed": False,
            "feedback": str(exc),
            "score": 0.0,
            "stdout": "",
            "stderr": "",
        }

    submission_mode = str(exercise.get("submission_mode", "function"))
    run = _run_user_code(
        submission_code,
        str(exercise.get("input_ir", "")),
        submission_mode=submission_mode,
    )

    try:
        metadata_ok, metadata_msg = _run_metadata_checks(exercise, run)
    except BaseException as validation_exc:
        return {
            "passed": False,
            "feedback": f"Internal metadata-check error: {type(validation_exc).__name__}: {validation_exc}",
            "score": 0.0,
            "stdout": run.get("stdout", ""),
            "stderr": str(run.get("stderr", "")) + "\n" + traceback.format_exc(),
            "result_preview": _result_preview(run.get("result")),
            "entrypoint_used": run.get("entrypoint"),
        }

    custom_ok = True
    custom_msg = ""
    custom_label = ""
    validation = exercise.get("validation", {})
    if isinstance(validation, dict) and (
        "custom_validator" in validation or "custom_validator_file" in validation
    ):
        return {
            "passed": False,
            "feedback": (
                "Explicit custom validator fields are not supported in convention mode. "
                "Use `<prefix>.validator.py` sidecar file."
            ),
            "score": 0.0,
            "stdout": run.get("stdout", ""),
            "stderr": run.get("stderr", ""),
            "result_preview": _result_preview(run.get("result")),
            "entrypoint_used": run.get("entrypoint"),
        }

    custom_file = _infer_custom_validator_file(exercise)
    if custom_file:
        custom_label = f"file:{custom_file}"
        try:
            validator = _load_custom_validator_from_file(exercise, str(custom_file))
        except BaseException as load_exc:
            return {
                "passed": False,
                "feedback": f"Failed to load custom validator file `{custom_file}`: {type(load_exc).__name__}: {load_exc}",
                "score": 0.0,
                "stdout": run.get("stdout", ""),
                "stderr": run.get("stderr", ""),
                "result_preview": _result_preview(run.get("result")),
                "entrypoint_used": run.get("entrypoint"),
            }
        try:
            custom_ok, custom_msg = _invoke_custom_validator(
                validator, run, exercise, _validator_helpers()
            )
        except BaseException as validation_exc:
            return {
                "passed": False,
                "feedback": f"Internal custom-validator error: {type(validation_exc).__name__}: {validation_exc}",
                "score": 0.0,
                "stdout": run.get("stdout", ""),
                "stderr": str(run.get("stderr", "")) + "\n" + traceback.format_exc(),
                "result_preview": _result_preview(run.get("result")),
                "entrypoint_used": run.get("entrypoint"),
            }

    passed = metadata_ok and custom_ok
    feedback_parts: list[str] = []
    if not metadata_ok:
        feedback_parts.append(metadata_msg)
    elif custom_file:
        feedback_parts.append("Metadata checks passed.")

    if custom_file:
        feedback_parts.append(custom_msg if custom_msg else "Custom validator passed.")
    elif metadata_ok:
        feedback_parts.append("Pass: metadata checks passed.")
    feedback = "\n".join([p for p in feedback_parts if p])

    exc = run.get("exc")
    if exc is not None and not passed:
        feedback = f"{feedback}\nRuntime exception: {type(exc).__name__}: {exc}"

    return {
        "passed": passed,
        "feedback": feedback,
        "score": 1.0 if passed else 0.0,
        "stdout": run.get("stdout", ""),
        "stderr": run.get("stderr", ""),
        "result_preview": _result_preview(run.get("result")),
        "entrypoint_used": run.get("entrypoint"),
        "custom_validator_used": custom_label,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Evaluate a single exercise submission.")
    parser.add_argument("--exercise-id", required=True, help="Exercise identifier (e.g. F01)")
    parser.add_argument(
        "--submission-file",
        required=True,
        type=Path,
        help="Path to Python submission file (solve/main/top-level supported).",
    )
    args = parser.parse_args()

    try:
        submission_code = args.submission_file.read_text(encoding="utf-8")
    except OSError as exc:
        print(
            json.dumps(
                {
                    "passed": False,
                    "feedback": f"Failed to read submission file: {exc}",
                    "score": 0.0,
                    "stdout": "",
                    "stderr": "",
                }
            )
        )
        return 0

    result = evaluate(args.exercise_id, submission_code)
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
