#!/usr/bin/env python3
"""Report the functional test-case count the README claims.

The README states how many GoogleTest and pytest cases the checked-in sources
define. That number is hand-maintained, so it drifts every time a test is added
and a reviewer has to notice. This script is the single source of truth; run it
with ``--check`` to fail when the committed README disagrees with the tree.

A "test case" here means one ``TEST``/``TEST_F``/``TEST_P`` declaration or one
``def test_`` function. It is deliberately not a ``colcon test-result`` record
count, which also includes test runners and ``ament_lint`` records.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROS2_WS = ROOT / "ros2_ws"

GTEST = re.compile(r"^\s*TEST(?:_F|_P)?\(", re.MULTILINE)
PYTEST = re.compile(r"^\s*def test_", re.MULTILINE)


def count(pattern: re.Pattern[str], suffix: str) -> int:
    total = 0
    for path in ROS2_WS.rglob(f"*{suffix}"):
        if "/build/" in path.as_posix() or "/install/" in path.as_posix():
            continue
        try:
            total += len(pattern.findall(path.read_text(encoding="utf-8")))
        except (OSError, UnicodeDecodeError):
            continue
    return total


def counts() -> tuple[int, int]:
    return count(GTEST, ".cpp"), count(PYTEST, ".py")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail when README.md or README.zh-CN.md states a different count",
    )
    args = parser.parse_args(argv)

    google_test, pytest_cases = counts()
    print(f"gtest={google_test} pytest={pytest_cases}")

    if not args.check:
        return 0

    stale = []
    for name in ("README.md", "README.zh-CN.md"):
        text = (ROOT / name).read_text(encoding="utf-8")
        if f"{google_test} GoogleTest" not in text and f"{google_test} 个 GoogleTest" not in text:
            stale.append(f"{name}: GoogleTest count is not {google_test}")
        if f"{pytest_cases} pytest" not in text and f"{pytest_cases} 个 pytest" not in text:
            stale.append(f"{name}: pytest count is not {pytest_cases}")
        # The platform table repeats the pair, so a fix that only touched the prose
        # would leave a second stale copy behind.
        if f"{google_test} GoogleTest cases plus {pytest_cases} pytest cases" not in text and (
            f"{google_test} 个 GoogleTest 用例和 {pytest_cases} 个 pytest 用例" not in text
        ):
            stale.append(f"{name}: platform table does not state {google_test}/{pytest_cases}")

    for problem in stale:
        print(f"STALE: {problem}", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
