#!/usr/bin/env python3
"""Regenerate expected stdout files for test/util/test_runner.py.

The util runner compares stdout byte-for-byte against files in test/util/data/.
This helper re-runs the same commands and overwrites the referenced
`output_cmp` files with the current stdout.

Typical use (regenerate a subset):

  python3 test/util/regenerate_golden_outputs.py \
    --only-output tt-delin1-out.json txcreate1.json
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
from pathlib import Path
import subprocess
import sys


def _load_build_env() -> dict[str, str]:
    cfg = configparser.ConfigParser()
    cfg.optionxform = str
    config_path = Path(__file__).resolve().parent / "../config.ini"
    with config_path.open(encoding="utf8") as f:
        cfg.read_file(f)
    return dict(cfg.items("environment"))


def _resolve_exec(test_obj: dict, buildenv: dict[str, str]) -> str:
    execprog = str(Path(buildenv["BUILDDIR"]) / "src" / (test_obj["exec"] + buildenv["EXEEXT"]))
    if test_obj["exec"] == "./bitcoin-util":
        return os.getenv("BITCOINUTIL", default=execprog)
    if test_obj["exec"] == "./bitcoin-tx":
        return os.getenv("BITCOINTX", default=execprog)
    return execprog


def _run_case(test_dir: Path, test_obj: dict, buildenv: dict[str, str]) -> subprocess.CompletedProcess[str]:
    execprog = _resolve_exec(test_obj, buildenv)
    argv = [execprog] + test_obj["args"]

    input_data = None
    if "input" in test_obj:
        input_data = (test_dir / test_obj["input"]).read_text(encoding="utf8")

    return subprocess.run(argv, input=input_data, capture_output=True, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--test-dir",
        default=str(Path(__file__).resolve().parent / "data"),
        help="Directory containing bitcoin-util-test.json and golden files",
    )
    parser.add_argument(
        "--input-json",
        default="bitcoin-util-test.json",
        help="Input JSON filename in --test-dir",
    )
    parser.add_argument(
        "--only-output",
        nargs="*",
        default=None,
        help="Only regenerate these output_cmp basenames",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Compute outputs but do not write files",
    )
    args = parser.parse_args()

    test_dir = Path(args.test_dir).resolve()
    input_path = test_dir / args.input_json
    buildenv = _load_build_env()

    tests = json.loads(input_path.read_text(encoding="utf8"))

    only = set(args.only_output) if args.only_output is not None else None

    rewritten: list[str] = []
    skipped: list[str] = []

    for test_obj in tests:
        output_cmp = test_obj.get("output_cmp")
        if not output_cmp:
            continue

        output_basename = Path(output_cmp).name
        if only is not None and output_basename not in only:
            continue

        proc = _run_case(test_dir, test_obj, buildenv)

        want_rc = int(test_obj.get("return_code", 0))
        if proc.returncode != want_rc:
            raise SystemExit(
                f"Refusing to write {output_basename}: return code {proc.returncode} != expected {want_rc}.\n"
                f"Command: {_resolve_exec(test_obj, buildenv)} {' '.join(test_obj['args'])}\n"
                f"stderr:\n{proc.stderr}"
            )

        want_error = test_obj.get("error_txt")
        if want_error and want_error not in proc.stderr:
            raise SystemExit(
                f"Refusing to write {output_basename}: expected error text not found in stderr.\n"
                f"Expected substring: {want_error}\n"
                f"stderr:\n{proc.stderr}"
            )

        out_path = test_dir / output_cmp
        new_stdout = proc.stdout

        if out_path.exists():
            old_stdout = out_path.read_text(encoding="utf8")
            if old_stdout == new_stdout:
                skipped.append(output_basename)
                continue

        if not args.dry_run:
            out_path.write_text(new_stdout, encoding="utf8")

        rewritten.append(output_basename)

    if rewritten:
        print("rewritten:")
        for name in rewritten:
            print(f"  {name}")
    if skipped:
        print("unchanged:")
        for name in skipped:
            print(f"  {name}")

    if only is not None:
        missing = sorted(only - set(rewritten) - set(skipped))
        if missing:
            raise SystemExit(f"Requested output(s) not found in input JSON: {missing}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
