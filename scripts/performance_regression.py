#!/usr/bin/env python3
"""Derive performance percentiles from the stage log and gate on a baseline.

The runtime already writes RuntimeEvent v2 JSONL through `StageEventLog`, and
the gateway, the executor and the bridge all append to the same file. This turns
that capture into admission, dispatch, ACK and cancel percentiles, records the
CPU and peak RSS of the runtime processes next to them, and compares the result
against a recorded baseline.

No new tracing and no new format: every input here is a line the runtime
already writes.

Usage:
    performance_regression.py --log stage_events.jsonl [--baseline PATH]
        [--tolerance PERCENT] [--pid PID ...] [--json] [--write-baseline]
        [--label CONFIGURATION]

Exit codes: 0 within tolerance (or a baseline was written), 1 a percentile
regressed past --tolerance, 2 the log could not be read or held no usable
samples.

Every number this prints is only meaningful together with the machine, load and
configuration it came from: pass --label and keep it with the figure.
"""

import argparse
import json
import pathlib
import sys

BASELINE = pathlib.Path(__file__).resolve().parents[1] / 'scripts' / 'performance_baseline.json'

# Ordered pairs of stages inside one trace. A trace is one task; the delta
# between two marks is the latency of the hop between them.
INTERVALS = (
    ('dispatch_ms', 'gateway_dispatch', 'action_receive'),
    ('ack_ms', 'can_frame_sent', 'can_ack_received'),
    ('cancel_detect_ms', 'cancel_requested', 'can_stop_sent'),
    ('stop_ack_ms', 'can_stop_sent', 'can_stop_acked'),
    ('cancel_total_ms', 'cancel_requested', 'can_stop_acked'),
)
# Stages that carry the interval in their own duration_ns instead of between
# two marks.
DURATION_STAGES = ('gateway_admission',)


def percentiles(values, points=(50, 95, 99)):
    """Nearest-rank percentiles; enough resolution for a tail check."""
    if not values:
        return {}
    ordered = sorted(values)
    result = {}
    for point in points:
        index = max(0, min(len(ordered) - 1, (point * len(ordered) + 99) // 100 - 1))
        result[f'p{point}'] = ordered[index]
    return result


def load_events(path):
    """Yield well-formed records, skipping lines another writer may have cut."""
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
            if not record.get('trace_id') or not record.get('stage'):
                continue
            timestamp = record.get('timestamp_ns')
            if not isinstance(timestamp, (int, float)):
                continue
            yield record


def collect(events):
    """Group stage marks per trace and derive the hop latencies."""
    marks = {}
    durations = {stage: [] for stage in DURATION_STAGES}
    for event in events:
        trace = str(event['trace_id'])
        stage = str(event['stage'])
        marks.setdefault(trace, {}).setdefault(stage, []).append(int(event['timestamp_ns']))
        if stage in durations:
            value = event.get('duration_ns')
            if isinstance(value, (int, float)) and value > 0:
                durations[stage].append(int(value))

    samples = {stage: [] for stage in DURATION_STAGES}
    for stage, values in durations.items():
        samples[stage].extend(values)
    # A trace that retried has several can_frame_sent marks; the ACK belongs to
    # the last attempt, so pair the last mark of the first stage with the first
    # mark of the second stage that follows it.
    for name, start, end in INTERVALS:
        samples[name] = []
        for trace_marks in marks.values():
            starts = trace_marks.get(start)
            ends = trace_marks.get(end)
            if not starts or not ends:
                continue
            # A retried command emits one start per attempt. The measured hop is
            # the one that produced the end mark: take the last start at or
            # before the first end, so an earlier attempt is not paired with a
            # later attempt's ACK.
            finish = min(ends)
            attempts = [stamp for stamp in starts if stamp <= finish]
            if not attempts:
                continue
            samples[name].append(finish - max(attempts))
    return {name: values for name, values in samples.items() if values}


def process_resources(pids):
    """CPU seconds and peak RSS for the given processes.

    VmHWM and the utime/stime totals are both high-water marks the kernel keeps
    for the process lifetime, so one read after the run is enough; this does not
    sample during it.
    """
    total_cpu_s = 0.0
    peak_rss_kb = 0
    seen = []
    for pid in pids:
        try:
            stat = pathlib.Path(f'/proc/{pid}/stat').read_text(encoding='utf-8')
            status = pathlib.Path(f'/proc/{pid}/status').read_text(encoding='utf-8')
        except OSError:
            continue
        # Field 2 (comm) is parenthesized and may contain spaces.
        fields = stat.rsplit(')', 1)[1].split()
        ticks = int(fields[11]) + int(fields[12])
        clock_ticks = 100  # USER_HZ; 100 on every Linux ABI this runs on.
        total_cpu_s += ticks / clock_ticks
        high_water = 0
        for line in status.splitlines():
            if line.startswith('VmHWM:'):
                high_water = int(line.split()[1])
        peak_rss_kb = max(peak_rss_kb, high_water)
        seen.append(pid)
    if not seen:
        return {}
    return {'cpu_s': round(total_cpu_s, 3), 'peak_rss_kb': peak_rss_kb,
            'processes': len(seen)}


def build_report(events, pids):
    report = {}
    for name, values in collect(events).items():
        if not values:
            continue
        report[name] = {
            'samples': len(values),
            'percentiles_ms': {key: round(value / 1e6, 3)
                               for key, value in percentiles(values).items()},
        }
    resources = process_resources(pids)
    if resources:
        report['resources'] = resources
    return report


def compare(report, baseline, tolerance):
    """Return the (metric, percentile, actual, expected) pairs past tolerance."""
    regressions = []
    for name, measured in report.items():
        if name == 'resources':
            continue
        expected = baseline.get(name, {}).get('percentiles_ms', {})
        for point, actual in measured['percentiles_ms'].items():
            limit = expected.get(point)
            if limit is None:
                continue
            if actual > limit * (1 + tolerance / 100):
                regressions.append((name, point, actual, limit))
    return regressions


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, help='stage event JSONL from one run')
    parser.add_argument('--baseline', default=str(BASELINE))
    parser.add_argument('--tolerance', type=float, default=50.0,
                        help='percent a percentile may exceed its baseline')
    parser.add_argument('--pid', type=int, action='append', default=[],
                        help='runtime process to report CPU and peak RSS for')
    parser.add_argument('--label', default='',
                        help='machine, load and configuration this run came from')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--write-baseline', action='store_true',
                        help='record this run as the baseline instead of comparing')
    parser.add_argument('--gate', action='store_true',
                        help='require a baseline and exit 1 on a regression')
    options = parser.parse_args(argv[1:])

    try:
        report = build_report(load_events(options.log), options.pid)
    except OSError as error:
        print(f'cannot read {options.log}: {error}', file=sys.stderr)
        return 2
    if not report or all(name == 'resources' for name in report):
        print('no stage intervals found in the log', file=sys.stderr)
        return 2

    if options.write_baseline:
        pathlib.Path(options.baseline).write_text(
            json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        print(f'baseline written to {options.baseline}', file=sys.stderr)
        if options.json:
            print(json.dumps({'label': options.label, 'report': report,
                              'regressions': []}, indent=2, sort_keys=True))
        return 0

    baseline_path = pathlib.Path(options.baseline)
    if not baseline_path.is_file():
        # Trend mode without a recorded baseline still has to publish the
        # numbers: refusing to print them is how a run ends up "green" with an
        # empty report. The comparison is skipped, and that is said out loud.
        if options.gate:
            print(f'no baseline at {baseline_path}; --gate needs one',
                  file=sys.stderr)
            return 2
        print(f'no baseline at {baseline_path}; reporting trend only',
              file=sys.stderr)
        regressions = []
    else:
        regressions = compare(report, json.loads(
            baseline_path.read_text(encoding='utf-8')), options.tolerance)

    if options.json:
        print(json.dumps({'label': options.label, 'report': report,
                          'regressions': regressions}, indent=2, sort_keys=True))
        return 1 if regressions else 0

    if options.label:
        print(f'configuration: {options.label}')
    resources = report.get('resources')
    if resources:
        # Process-lifetime CPU, not per request: state it rather than imply a
        # per-call cost this measurement cannot produce.
        print(f'resources: cpu_s={resources["cpu_s"]} (process lifetime) '
              f'peak_rss_kb={resources["peak_rss_kb"]} '
              f'processes={resources["processes"]}')
    print(f'{"interval":<20}{"samples":>8}{"p50_ms":>10}{"p95_ms":>10}{"p99_ms":>10}')
    for name, measured in sorted(report.items()):
        if name == 'resources':
            continue
        points = measured['percentiles_ms']
        print(f'{name:<20}{measured["samples"]:>8}'
              f'{points.get("p50", 0):>10.3f}{points.get("p95", 0):>10.3f}'
              f'{points.get("p99", 0):>10.3f}')

    for name, point, actual, limit in regressions:
        print(f'REGRESSED {name} {point}: {actual} ms vs baseline {limit} ms '
              f'(+{options.tolerance:.0f}% allowed)')
    if not regressions:
        print(f'all intervals within {options.tolerance:.0f}% of the baseline')
    return 1 if regressions else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
