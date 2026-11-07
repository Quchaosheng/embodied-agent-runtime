"""
Checks for the stage latency report.

The report reads the RuntimeEvent v2 JSONL the runtime already writes, so the
fixture mirrors that exact field set rather than a simplified one.
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


def _script_path():
    """Find the report next to the sources, if this tree has it."""
    override = os.environ.get('STAGE_LATENCY_REPORT')
    if override:
        return pathlib.Path(override)
    for parent in pathlib.Path(__file__).resolve().parents:
        candidate = parent / 'scripts' / 'stage_latency_report.py'
        if candidate.is_file():
            return candidate
    return None


# A tree can carry the package without the repository-level scripts directory
# (for example a source-only export). Skip rather than fail there: the report
# itself is repository tooling, not part of the installed runtime.
SCRIPT = _script_path()


def stage_line(stage, duration_ns, trace='trace-1'):
    return json.dumps({
        'trace_id': trace,
        'sequence_id': 1,
        'source_node': 'task_executor',
        'stage': stage,
        'timestamp_ns': 1,
        'event_name': f'{stage}_start',
        'event_type': 'stage',
        'clock_id': 'monotonic',
        'duration_ns': duration_ns,
        'status': 'observed',
        'reason_code': '',
        'extra_json': '{}',
    })


@unittest.skipIf(SCRIPT is None, 'stage_latency_report.py is not present in this tree')
class StageLatencyReportTest(unittest.TestCase):

    def run_report(self, lines, *extra):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'events.jsonl'
            path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            return subprocess.run(
                [sys.executable, str(SCRIPT), str(path), *extra],
                capture_output=True, text=True, check=False)

    def test_reports_percentiles_per_stage(self):
        lines = [stage_line('can_ack_wait', value * 1_000_000) for value in range(1, 101)]
        result = self.run_report(lines, '--json')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['can_ack_wait']['samples'], 100)
        self.assertEqual(report['can_ack_wait']['percentiles_ms']['p50'], 50.0)
        self.assertEqual(report['can_ack_wait']['percentiles_ms']['p99'], 99.0)

    def test_ignores_zero_duration_and_broken_lines(self):
        lines = [
            stage_line('action_receive', 0),
            'this is not json',
            stage_line('can_receive', 2_000_000),
        ]
        result = self.run_report(lines)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('can_receive', result.stdout)
        self.assertNotIn('action_receive', result.stdout)

    def test_fails_when_no_duration_is_present(self):
        result = self.run_report([stage_line('action_receive', 0)])
        self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
