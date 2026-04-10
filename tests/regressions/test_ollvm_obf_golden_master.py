"""Golden-master comparisons against stored public-pass ``ollvm-obf`` outputs.

The fixture files under ``tests/golden/ollvm_obf/`` are generated manually from
an external C++ ``ollvm-obf`` build, then checked into this repository.
Repo tests only validate that the Python obfuscator reproduces those stored
outputs; they do not depend on a local ``omill`` checkout.
"""

from __future__ import annotations

from functools import lru_cache
import difflib
from pathlib import Path
import re

import llvm
import pytest

from tools.obfuscation.ollvm_obf import main as python_ollvm_obf
from tools.obfuscation.ollvm_obf_golden_cases import GOLDEN_CASES, case_slug


LABEL_DEF_RE = re.compile(r"^([A-Za-z$._0-9-]+):(.*)$")
LOCAL_TOKEN_RE = re.compile(r'%(?:"[^"]+"|[-A-Za-z$._0-9]+)')
GLOBAL_TOKEN_RE = re.compile(r'@(?:"[^"]+"|[-A-Za-z$._0-9]+)')
FIXTURES_DIR = Path("tests/golden/ollvm_obf")


@lru_cache(maxsize=1)
def target_machine() -> llvm.TargetMachine:
    llvm.initialize_all_target_infos()
    llvm.initialize_all_targets()
    llvm.initialize_all_target_mcs()
    llvm.initialize_all_asm_printers()
    llvm.initialize_all_asm_parsers()

    triple = llvm.get_default_target_triple()
    target = llvm.get_target_from_triple(triple)
    assert target is not None
    return llvm.create_target_machine(
        target,
        triple,
        "generic",
        "",
        llvm.CodeGenOptLevel.Default,
        llvm.RelocMode.Default,
        llvm.CodeModel.Default,
    )


def canonicalize_ir(text: str, cleanup_pipeline: str | None) -> str:
    if cleanup_pipeline is not None:
        with llvm.create_context() as ctx:
            with ctx.parse_ir(text) as mod:
                llvm.run_passes(
                    mod,
                    cleanup_pipeline,
                    target_machine=target_machine(),
                    options=llvm.PassBuilderOptions(),
                )
                mod.name = "<module>"
                mod.source_filename = "<source>"
                text = mod.to_string()

    global_map: dict[str, str] = {}

    def global_name(token: str) -> str:
        if token.startswith("@llvm."):
            return token
        mapped = global_map.get(token)
        if mapped is None:
            mapped = f"@g{len(global_map)}"
            global_map[token] = mapped
        return mapped

    preprocessed_lines: list[str] = []
    for raw_line in text.splitlines():
        if raw_line.startswith(";"):
            continue

        line = re.sub(r"\s*;.*$", "", raw_line)
        if not line.strip():
            continue
        if line.startswith("attributes #"):
            continue

        line = re.sub(r"\s+#\d+(?=\b|\s|$)", "", line)
        line = re.sub(
            r"@llvm\.x86\.bmi\.([^.\s(]+)\.(32|64)\.i(32|64)",
            r"@llvm.x86.bmi.\1.\2",
            line,
        )
        if line.startswith("source_filename = "):
            line = 'source_filename = "<source>"'

        preprocessed_lines.append(line.rstrip())

    # LLVM may print the first basic block of a function with or without an
    # explicit label. Remove that cosmetic difference before token numbering so
    # later block names stay aligned.
    lines_without_entry_labels: list[str] = []
    first_block_pending = False
    for line in preprocessed_lines:
        if line.startswith("define "):
            lines_without_entry_labels.append(line)
            first_block_pending = True
            continue
        if line == "}":
            lines_without_entry_labels.append(line)
            first_block_pending = False
            continue
        if first_block_pending and LABEL_DEF_RE.match(line) is not None:
            first_block_pending = False
            continue
        if first_block_pending:
            first_block_pending = False
        lines_without_entry_labels.append(line)

    normalized_lines: list[str] = []
    i = 0
    while i < len(lines_without_entry_labels):
        line = lines_without_entry_labels[i]
        if not line.startswith("define "):
            line = GLOBAL_TOKEN_RE.sub(lambda m: global_name(m.group(0)), line)
            normalized_lines.append(line.rstrip())
            i += 1
            continue

        j = i + 1
        while j < len(lines_without_entry_labels) and lines_without_entry_labels[j] != "}":
            j += 1
        function_chunk = lines_without_entry_labels[i : j + 1]
        label_defs = {
            f"%{m.group(1)}"
            for chunk_line in function_chunk
            if (m := LABEL_DEF_RE.match(chunk_line)) is not None
        }
        value_map: dict[str, str] = {}
        label_map: dict[str, str] = {}

        def value_name(token: str) -> str:
            mapped = value_map.get(token)
            if mapped is None:
                mapped = f"%v{len(value_map)}"
                value_map[token] = mapped
            return mapped

        def label_name(token: str) -> str:
            mapped = label_map.get(token)
            if mapped is None:
                mapped = f"%bb{len(label_map)}"
                label_map[token] = mapped
            return mapped

        for chunk_line in function_chunk:
            label_match = LABEL_DEF_RE.match(chunk_line)
            if label_match is not None:
                label_token = f"%{label_match.group(1)}"
                mapped_label = label_name(label_token)[1:]
                chunk_line = f"{mapped_label}:{label_match.group(2)}"

            chunk_line = GLOBAL_TOKEN_RE.sub(lambda m: global_name(m.group(0)), chunk_line)
            chunk_line = LOCAL_TOKEN_RE.sub(
                lambda m: label_name(m.group(0)) if m.group(0) in label_defs else value_name(m.group(0)),
                chunk_line,
            )
            normalized_lines.append(chunk_line.rstrip())

        i = j + 1

    return "\n".join(normalized_lines).strip() + "\n"


def run_python_tool(args: tuple[str, ...], input_path: Path, output_path: Path) -> str:
    rc = python_ollvm_obf([*args, str(input_path), "-o", str(output_path)])
    assert rc == 0
    return output_path.read_text(encoding="utf-8")


@pytest.mark.parametrize("case", GOLDEN_CASES, ids=lambda case: case.name)
def test_ollvm_obf_matches_stored_golden(tmp_path: Path, case) -> None:
    case_dir = FIXTURES_DIR / case_slug(case.name)
    input_path = case_dir / "input.ll"
    expected_path = case_dir / "expected.ll"

    assert input_path.is_file(), f"Missing golden input: {input_path}"
    assert expected_path.is_file(), f"Missing golden output: {expected_path}"

    py_output = run_python_tool(case.args, input_path, tmp_path / "actual.ll")
    expected_output = expected_path.read_text(encoding="utf-8")

    expected_canon = canonicalize_ir(expected_output, case.cleanup_pipeline)
    py_canon = canonicalize_ir(py_output, case.cleanup_pipeline)

    if expected_canon != py_canon:
        diff = "\n".join(
            difflib.unified_diff(
                expected_canon.splitlines(),
                py_canon.splitlines(),
                fromfile="golden",
                tofile="python",
                lineterm="",
            )
        )
        pytest.fail(f"Python port diverged from stored golden master:\n{diff}")


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
