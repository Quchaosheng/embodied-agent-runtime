"""
Checks for the statistics index benchmark.

The benchmark is the evidence for the index change, so its result shape and its
regression verdict are pinned here rather than discovered in CI.
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


def _script_path():
    """Find the benchmark either in a source checkout or in an installed prefix."""
    override = os.environ.get('STATS_INDEX_BENCHMARK')
    if override:
        return pathlib.Path(override)
    for parent in pathlib.Path(__file__).resolve().parents:
        candidate = parent / 'scripts' / 'benchmark_stats_indexes.py'
        if candidate.is_file():
            return candidate
    raise AssertionError('benchmark_stats_indexes.py not found from the test location')


SCRIPT = _script_path()


def run(*args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True, text=True, check=False)


class StatsIndexBenchmarkTest(unittest.TestCase):

    def test_reports_both_configurations_per_row_count(self):
        result = run('--rows', '200', '--repeat', '2')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('without:', result.stdout)
        self.assertIn('with:', result.stdout)
        self.assertIn('stats_ms', result.stdout)

    def test_skips_a_row_count_the_committed_baseline_does_not_record(self):
        # Host timing is not asserted here: this test only pins that an
        # unrecorded row count is skipped instead of treated as a regression.
        result = run('--rows', '200', '--repeat', '2')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count('REGRESSED'), 0)

    def test_fails_when_the_baseline_allows_no_headroom(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'baseline.json'
            path.write_text(json.dumps({'200': {'with_indexes': {'stats_ms': 0.000001}}}),
                            encoding='utf-8')
            result = run('--rows', '200', '--repeat', '2', '--baseline', str(path),
                         '--tolerance', '0')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('REGRESSED', result.stdout)

    def test_records_a_baseline_without_touching_the_committed_one(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'baseline.json'
            result = run('--rows', '200', '--repeat', '2', '--baseline', str(path),
                         '--write-baseline')
            self.assertEqual(result.returncode, 0, result.stderr)
            recorded = json.loads(path.read_text(encoding='utf-8'))['200']['with_indexes']
            self.assertGreater(recorded['stats_ms'], 0)

    def test_rejects_an_impossible_row_count(self):
        result = run('--rows', 'not-a-number')
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
