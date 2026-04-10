#!/usr/bin/env -S uv run
"""Generate stored ollvm-obf golden-master fixtures.

This is a maintenance script, not part of the normal test flow.
It requires an externally built public-pass C++ ``ollvm-obf`` binary.

Typical workflow:

1. Obtain/build ``omill`` separately.
2. Build the public ``ollvm-obf`` tool.
3. Run:

   uv run tools/obfuscation/generate_ollvm_obf_goldens.py \
     --ollvm-obf /path/to/ollvm-obf[.exe]

This overwrites the checked-in fixtures under ``tests/golden/ollvm_obf``.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import textwrap

from tools.obfuscation.ollvm_obf_golden_cases import GOLDEN_CASES, case_slug


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ollvm-obf",
        required=True,
        help="Path to the external C++ ollvm-obf binary",
    )
    parser.add_argument(
        "--output-dir",
        default="tests/golden/ollvm_obf",
        help="Directory where golden fixtures are written",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tool = Path(args.ollvm_obf)
    if not tool.is_file():
        raise SystemExit(f"ollvm-obf not found: {tool}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    for case in GOLDEN_CASES:
        case_dir = output_dir / case_slug(case.name)
        case_dir.mkdir(parents=True, exist_ok=True)

        input_path = case_dir / "input.ll"
        expected_path = case_dir / "expected.ll"
        meta_path = case_dir / "meta.json"

        input_path.write_text(
            textwrap.dedent(case.input_ir).strip() + "\n",
            encoding="utf-8",
        )

        result = subprocess.run(
            [str(tool), *case.args, str(input_path), "-o", str(expected_path)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise SystemExit(
                f"failed to generate fixture for {case.name}:\n{result.stderr}"
            )

        meta_path.write_text(
            json.dumps(
                {
                    "name": case.name,
                    "args": list(case.args),
                    "cleanup_pipeline": case.cleanup_pipeline,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

        print(f"wrote {case_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
