"""
Checks for the performance regression suite.

The suite is the harness every other performance claim is judged against, so
the interval derivation and the pass/fail verdict are pinned here rather than
discovered in CI. Fixtures mirror the RuntimeEvent v2 field set the runtime
emits, not a simplified one.
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


def _script_path():
    """Find the suite next to the sources, if this tree has it."""
    override = os.environ.get('PERFORMANCE_REGRESSION')
    if override:
        return pathlib.Path(override)
    for parent in pathlib.Path(__file__).resolve().parents:
        candidate = parent / 'scripts' / 'performance_regression.py'
        if candidate.is_file():
            return candidate
    return None


# A source-only export can carry the package without the repository scripts.
SCRIPT = _script_path()


def stage_line(stage, timestamp_ns, duration_ns=0, trace='trace-1', extra_json='{}'):
    return json.dumps({
        'trace_id': trace,
        'sequence_id': 1,
        'source_node': 'task_executor',
        'stage': stage,
        'timestamp_ns': timestamp_ns,
        'event_name': stage,
        'event_type': 'action',
        'pid': 1,
        'tid': 1,
        'host_id': 'host',
        'clock_id': 'monotonic',
        'duration_ns': duration_ns,
        'status': 'observed',
        'reason_code': '',
        'extra_json': extra_json,
    })


@unittest.skipIf(SCRIPT is None, 'performance_regression.py is not present in this tree')
class PerformanceRegressionTest(unittest.TestCase):

    def run_suite(self, lines, *extra, baseline=None):
        with tempfile.TemporaryDirectory() as directory:
            log = pathlib.Path(directory) / 'events.jsonl'
            log.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            args = [sys.executable, str(SCRIPT), '--log', str(log), *extra]
            if baseline is not None:
                path = pathlib.Path(directory) / 'baseline.json'
                path.write_text(json.dumps(baseline), encoding='utf-8')
                args += ['--baseline', str(path)]
            return subprocess.run(args, capture_output=True, text=True, check=False)

    def write_baseline(self, directory, payload):
        path = pathlib.Path(directory) / 'baseline.json'
        path.write_text(json.dumps(payload), encoding='utf-8')
        return str(path)

    def cancel_trace(self, base_ns, cancel_ms=1.0, stop_ack_ms=4.0, trace='trace-1'):
        """One cancel: request, STOP sent, STOP ACK, at the given offsets."""
        step = 1_000_000  # 1 ms in ns
        return [
            stage_line('cancel_requested', base_ns, trace=trace),
            stage_line('can_stop_sent', base_ns + int(cancel_ms * step), trace=trace),
            stage_line('can_stop_acked',
                       base_ns + int((cancel_ms + stop_ack_ms) * step), trace=trace),
        ]

    def test_derives_cancel_intervals_between_two_stage_marks(self):
        result = self.run_suite(self.cancel_trace(1_000_000_000), '--json', '--write-baseline')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)['report']
        self.assertEqual(report['cancel_detect_ms']['percentiles_ms']['p50'], 1.0)
        self.assertEqual(report['stop_ack_ms']['percentiles_ms']['p50'], 4.0)
        self.assertEqual(report['cancel_total_ms']['percentiles_ms']['p50'], 5.0)

    def test_pairs_the_ack_after_the_last_attempt_not_before_it(self):
        # A retried command has two sends; the ACK that counts follows the
        # later one, and must not be paired with the earlier attempt.
        lines = [
            stage_line('can_frame_sent', 1_000_000_000),
            stage_line('can_frame_sent', 1_050_000_000),
            stage_line('can_ack_received', 1_060_000_000),
        ]
        result = self.run_suite(lines, '--json', '--write-baseline')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)['report']
        # 10 ms from the later send, which is the attempt the ACK belongs to.
        self.assertEqual(report['ack_ms']['percentiles_ms']['p50'], 10.0)

    def test_admission_uses_its_own_duration_field(self):
        lines = [stage_line('gateway_admission', 1_000_000_000, duration_ns=2_500_000)]
        result = self.run_suite(lines, '--json', '--write-baseline')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)['report']
        self.assertEqual(report['gateway_admission']['percentiles_ms']['p50'], 2.5)

    def test_fails_a_regression_past_the_tolerance(self):
        result = self.run_suite(
            self.cancel_trace(1_000_000_000, stop_ack_ms=4.0), '--tolerance', '50',
            baseline={'stop_ack_ms': {'percentiles_ms': {'p50': 1.0, 'p95': 1.0, 'p99': 1.0}}})
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('REGRESSED stop_ack_ms', result.stdout)

    def test_passes_a_regression_inside_the_tolerance(self):
        result = self.run_suite(
            self.cancel_trace(1_000_000_000, stop_ack_ms=4.0), '--tolerance', '50',
            baseline={'stop_ack_ms': {'percentiles_ms': {'p50': 4.2, 'p95': 4.2, 'p99': 4.2}}})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('within', result.stdout)

    def test_ignores_an_interval_the_baseline_does_not_record(self):
        result = self.run_suite(
            self.cancel_trace(1_000_000_000),
            baseline={'stop_ack_ms': {'percentiles_ms': {'p50': 99.0}}})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_records_a_baseline_with_the_configuration_label(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'recorded.json'
            result = self.run_suite(
                self.cancel_trace(1_000_000_000), '--write-baseline', '--baseline', str(path),
                '--label', 'ubuntu24.04 x86_64 idle')
            self.assertEqual(result.returncode, 0, result.stderr)
            recorded = json.loads(path.read_text(encoding='utf-8'))
            self.assertGreater(recorded['cancel_total_ms']['percentiles_ms']['p50'], 0)

    def test_reports_a_missing_baseline_instead_of_passing_silently(self):
        result = self.run_suite(
            self.cancel_trace(1_000_000_000), '--baseline', '/nonexistent/baseline.json')
        self.assertEqual(result.returncode, 2)
        self.assertIn('no baseline', result.stderr)

    def test_reports_process_resources_when_given_a_pid(self):
        result = self.run_suite(
            self.cancel_trace(1_000_000_000), '--pid', str(os.getpid()), '--json',
            '--write-baseline')
        self.assertEqual(result.returncode, 0, result.stderr)
        resources = json.loads(result.stdout)['report']['resources']
        self.assertGreater(resources['peak_rss_kb'], 0)
        self.assertGreaterEqual(resources['cpu_s'], 0)

    def test_fails_when_the_log_holds_no_interval(self):
        result = self.run_suite([stage_line('action_receive', 1)])
        self.assertEqual(result.returncode, 2)

    def test_fails_when_the_log_is_missing(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--log', '/nonexistent/events.jsonl'],
            capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
