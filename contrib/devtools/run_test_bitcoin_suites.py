#!/usr/bin/env python3
"""Run src/test/test_bitcoin top-level suites individually.

Motivation: `make check` runs the unit test binary as a whole. For fork bring-up,
we want per-suite pass/fail granularity and a machine-readable report.

Output:
- Writes JSON progress to doc/bng/tests/logs/test_bitcoin.suites.json
- Writes per-suite logs to doc/bng/tests/logs/test_bitcoin.<suite>.log

Exit codes:
- 0 if all suites pass
- 1 if any suite fails
- 2 on runner error (cannot enumerate suites, missing binary, etc.)
"""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path


def _list_top_level_suites(test_binary: Path) -> list[str]:
    proc = subprocess.run(
        [str(test_binary), "--list_content"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    # In this repo/config, Boost.Test prints --list_content to stderr.
    content = (proc.stdout or "") + (proc.stderr or "")

    suites: list[str] = []
    for line in content.splitlines():
        if line.startswith(" "):
            continue
        stripped = line.strip()
        if stripped.endswith("*"):
            suites.append(stripped[:-1])

    return suites


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    test_binary = repo_root / "src" / "test" / "test_bitcoin"
    if not test_binary.exists():
        print(f"ERROR: missing unit test binary: {test_binary}", file=sys.stderr)
        return 2

    logdir = repo_root / "doc" / "bng" / "tests" / "logs"
    logdir.mkdir(parents=True, exist_ok=True)
    report_path = logdir / "test_bitcoin.suites.json"

    suites = _list_top_level_suites(test_binary)
    if not suites:
        print("ERROR: failed to enumerate test suites (empty list)", file=sys.stderr)
        return 2

    results: dict[str, dict[str, object]] = {}
    failed: list[str] = []

    start = time.time()
    for idx, suite in enumerate(suites, 1):
        logfile = logdir / f"test_bitcoin.{suite}.log"
        with logfile.open("w", encoding="utf-8") as f:
            proc = subprocess.run(
                [str(test_binary), f"--run_test={suite}"],
                stdout=f,
                stderr=subprocess.STDOUT,
            )

        ok = proc.returncode == 0
        results[suite] = {
            "ok": ok,
            "returncode": proc.returncode,
            "log": str(logfile.relative_to(repo_root)),
        }
        if not ok:
            failed.append(suite)

        # Persist progress after each suite.
        report = {
            "binary": str(test_binary.relative_to(repo_root)),
            "suite_count": len(suites),
            "completed": idx,
            "pass_count": idx - len(failed),
            "fail_count": len(failed),
            "failed_suites": failed,
            "results": results,
        }
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        if idx % 10 == 0 or idx == len(suites):
            elapsed = int(time.time() - start)
            print(f"[{idx}/{len(suites)}] pass={idx-len(failed)} fail={len(failed)} elapsed={elapsed}s")

    if failed:
        print("FAILED suites:")
        for s in failed:
            print(f"- {s}")
        return 1

    print("All suites passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
