#!/usr/bin/env python3
"""Measure what the task_runs / request_idempotency indexes cost and buy.

`stats()` runs count, max, a GROUP BY and three ordered percentile probes, and
the admission path runs a retention prune, all against the same SQLite file.
This builds throwaway databases with the runtime's own schema, runs the same
statement shapes the runtime runs, and reports read time with and without the
indexes next to the write time the indexes charge.

Usage:
    benchmark_stats_indexes.py [--rows 1000,10000] [--repeat 6] [--baseline PATH]

Absolute numbers are host-specific: report the machine, load and row counts
with any figure taken from here. Exit code 1 means the timings regressed past
--tolerance (percent) relative to the recorded baseline; 2 means the benchmark
could not run.
"""

import argparse
import json
import pathlib
import sqlite3
import statistics
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASELINE = ROOT / 'scripts' / 'stats_indexes_baseline.json'

# Same statements the runtime issues; see ros2_ws/src/runtime_history/src.
SCHEMA = (
    'CREATE TABLE IF NOT EXISTS task_runs ('
    "task_id TEXT PRIMARY KEY, request_id TEXT NOT NULL DEFAULT '',"
    "workflow_id TEXT NOT NULL DEFAULT '', goal_id TEXT NOT NULL DEFAULT '',"
    'target_id TEXT NOT NULL, action_status INTEGER NOT NULL, outcome INTEGER NOT NULL,'
    'error_code INTEGER NOT NULL, duration_ms INTEGER NOT NULL, message TEXT NOT NULL,'
    'completed_at_ns INTEGER NOT NULL);'
    'CREATE TABLE IF NOT EXISTS request_idempotency ('
    'request_id TEXT PRIMARY KEY, fingerprint TEXT NOT NULL, state TEXT NOT NULL,'
    'outcome INTEGER NOT NULL, error_code INTEGER NOT NULL, message TEXT NOT NULL,'
    'updated_at_ns INTEGER NOT NULL)'
)
INDEXES = (
    'CREATE INDEX IF NOT EXISTS task_runs_duration_ms ON task_runs(duration_ms);'
    'CREATE INDEX IF NOT EXISTS task_runs_outcome ON task_runs(outcome);'
    'CREATE INDEX IF NOT EXISTS request_idempotency_updated_at_ns '
    'ON request_idempotency(updated_at_ns)'
)
RECLAIMABLE = ('COMPLETED', 'CANCELED', 'SAFE_STOP', 'DEVICE_FAULT', 'ABORTED', 'REJECTED',
               'TIMED_OUT')
STATS = ('SELECT COUNT(*),MAX(duration_ms) FROM task_runs',
         'SELECT outcome,COUNT(*) FROM task_runs GROUP BY outcome',
         'SELECT duration_ms FROM task_runs ORDER BY duration_ms LIMIT 1 OFFSET ?')
PRUNE = ('DELETE FROM request_idempotency WHERE updated_at_ns < ? AND state IN (?,?,?,?,?,?,?) '
         'AND request_id NOT IN (SELECT request_id FROM request_idempotency '
         'WHERE updated_at_ns >= ? ORDER BY updated_at_ns DESC LIMIT ?)')


def fill(database, rows):
    """One transaction per record, the way Store::insert() and put_idempotency() run."""
    started = time.perf_counter()
    task = ('INSERT OR IGNORE INTO task_runs (task_id,request_id,workflow_id,goal_id,target_id,'
            'action_status,outcome,error_code,duration_ms,message,completed_at_ns) '
            'VALUES (?,?,?,?,?,?,?,?,?,?,?)')
    ledger = ('INSERT INTO request_idempotency (request_id,fingerprint,state,outcome,error_code,'
              'message,updated_at_ns) VALUES (?,?,?,?,?,?,?) ON CONFLICT(request_id) DO UPDATE '
              'SET state=excluded.state,updated_at_ns=excluded.updated_at_ns '
              'WHERE request_idempotency.fingerprint=excluded.fingerprint')
    for index in range(rows):
        database.execute('BEGIN IMMEDIATE')
        database.execute(task, (f'task-{index}', 'request', 'single_task', 'goal', 'arm', 1,
                                index % 4, 0, 10 + (index * 7919) % 5000, 'completed',
                                1000000 + index))
        database.execute('COMMIT')
        database.execute(ledger, (f'request-{index}', 'fp', 'COMPLETED', 0, 0, 'done',
                                  1000 + index))
    return time.perf_counter() - started


def stats(database):
    count, _ = database.execute(STATS[0]).fetchone()
    database.execute(STATS[1]).fetchall()
    for point in (50, 95, 99):
        # Same nearest-rank offset as runtime_history::nearest_rank_offset.
        offset = (point * count + 99) // 100 - 1
        database.execute(STATS[2], (offset,)).fetchone()


def prune(database, rows):
    horizon = 1000 + rows // 10
    parameters = (horizon, *RECLAIMABLE, horizon, rows)
    database.execute(PRUNE, parameters).rowcount


def measure(rows, repeat, with_indexes):
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / 'bench.sqlite3'
        database = sqlite3.connect(path, isolation_level=None)
        try:
            database.execute('PRAGMA journal_mode=WAL')
            database.execute('PRAGMA synchronous=NORMAL')
            database.executescript(SCHEMA)
            if with_indexes:
                database.executescript(INDEXES)
            write_seconds = fill(database, rows)
            started = time.perf_counter()
            prune(database, rows)
            prune_seconds = time.perf_counter() - started
            stats(database)  # warm-up, excluded from the samples
            samples = []
            for _ in range(repeat):
                started = time.perf_counter()
                stats(database)
                samples.append((time.perf_counter() - started) * 1000)
            return {'write_ms': round(write_seconds * 1000, 1),
                    'prune_ms': round(prune_seconds * 1000, 2),
                    'stats_ms': round(statistics.median(samples), 3)}
        finally:
            database.close()


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', default='1000,10000')
    parser.add_argument('--repeat', type=int, default=6)
    parser.add_argument('--tolerance', type=float, default=50.0,
                        help='percent a stats_ms sample may exceed its baseline')
    parser.add_argument('--baseline', default=str(BASELINE),
                        help='recorded timings to check against (default: the committed one)')
    parser.add_argument('--write-baseline', action='store_true',
                        help='record the current timings as the baseline instead of checking')
    options = parser.parse_args(argv[1:])

    results = {}
    for rows in (int(value) for value in options.rows.split(',')):
        plain = measure(rows, options.repeat, with_indexes=False)
        indexed = measure(rows, options.repeat, with_indexes=True)
        results[str(rows)] = {'without_indexes': plain, 'with_indexes': indexed}
        print(f'{rows:>7} rows  without: {plain}')
        print(f'{rows:>7} rows  with:    {indexed}')

    path = pathlib.Path(options.baseline)
    if options.write_baseline:
        path.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        print(f'baseline written to {path}')
        return 0

    baseline = json.loads(path.read_text(encoding='utf-8'))
    failed = False
    for rows, measured in results.items():
        expected = baseline.get(rows, {}).get('with_indexes', {}).get('stats_ms')
        if expected is None:
            continue
        actual = measured['with_indexes']['stats_ms']
        limit = expected * (1 + options.tolerance / 100)
        status = 'ok' if actual <= limit else 'REGRESSED'
        failed |= actual > limit
        print(f'{rows:>7} rows  stats_ms {actual} vs baseline {expected} '
              f'(+{options.tolerance:.0f}% allowed): {status}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
