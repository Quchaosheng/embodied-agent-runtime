#!/usr/bin/env python3
"""Derive per-stage latency percentiles from an existing stage event log.

The runtime already writes RuntimeEvent v2 JSONL via `StageEventLog`; this
reads that file and reports P50/P95/P99 for each `duration_ns` it carries, so a
performance claim can be checked against the same evidence the runtime
produces. It adds no tracing and no new format.

Usage:
    stage_latency_report.py <stage_events.jsonl> [--json]

Exit codes: 0 report written, 2 input unreadable or no durations found.
"""

import argparse
import json
import pathlib
import sys


def percentiles(values, points=(50, 95, 99)):
    """Nearest-rank percentiles; enough resolution for a tail check."""
    if not values:
        return {}
    ordered = sorted(values)
    result = {}
    for point in points:
        # Nearest rank avoids interpolating across a gap in discrete samples.
        index = max(0, min(len(ordered) - 1, (point * len(ordered) + 99) // 100 - 1))
        result[f'p{point}'] = ordered[index]
    return result


def load_stages(path):
    """Yield (stage, duration_ns) for every well-formed line with a duration."""
    with pathlib.Path(path).open(encoding='utf-8') as handle:
        for number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                print(f'skip line {number}: not JSON', file=sys.stderr)
                continue
            if not isinstance(record, dict):
                continue
            stage = record.get('stage') or record.get('event_name')
            duration = record.get('duration_ns')
            if not stage or not isinstance(duration, (int, float)) or duration <= 0:
                continue
            yield str(stage), int(duration)


def build_report(stages):
    grouped = {}
    for stage, duration in stages:
        grouped.setdefault(stage, []).append(duration)
    return {
        stage: {
            'samples': len(values),
            'median_ms': round(sorted(values)[len(values) // 2] / 1e6, 3),
            'percentiles_ms': {
                key: round(value / 1e6, 3)
                for key, value in percentiles(values).items()
            },
        }
        for stage, values in sorted(grouped.items())
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', help='stage event JSONL produced by the runtime')
    parser.add_argument('--json', action='store_true', help='emit JSON instead of a table')
    arguments = parser.parse_args()

    try:
        report = build_report(load_stages(arguments.log))
    except OSError as error:
        print(f'cannot read {arguments.log}: {error}', file=sys.stderr)
        return 2

    if not report:
        print('no stage durations found in the log', file=sys.stderr)
        return 2

    if arguments.json:
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0

    print(f'{"stage":<28}{"samples":>8}{"p50_ms":>10}{"p95_ms":>10}{"p99_ms":>10}')
    for stage, values in report.items():
        points = values['percentiles_ms']
        print(
            f'{stage:<28}{values["samples"]:>8}'
            f'{points.get("p50", 0):>10.3f}{points.get("p95", 0):>10.3f}'
            f'{points.get("p99", 0):>10.3f}'
        )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
