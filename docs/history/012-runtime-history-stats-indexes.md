# Runtime History Statistics Indexes

`stats()` walked `task_runs` for the percentile probes and `prune_idempotency()`
scanned the retention window, with no supporting index on either table. Both
index sets are now created on open, including for a database written before
they existed.

Measured with `scripts/benchmark_stats_indexes.py` on this host (Ubuntu 24.04,
x86_64, `-O2`, system SQLite, one transaction per write as the runtime runs it):

| rows | stats ms, no index | stats ms, indexed | write ms, no index | write ms, indexed |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1.84 | 0.208 | 76.5 | 96.2 |
| 10,000 | 11.05 | 2.53 | 728.7 | 1,215.0 |

The read side gains an order of magnitude; the write side pays roughly 25-65%
more on this synthetic fill, which is the cost that made the indexes
worth measuring rather than assuming. The benchmark records that baseline and
fails when the indexed read time drifts past its tolerance.

Verification: `colcon test --packages-select runtime_history` (`test_store` 20
GoogleTest cases, 8 pytest cases) and `python3
scripts/benchmark_stats_indexes.py` on this host.
These figures are host-specific: they are query time on one machine, not an
end-to-end speedup, and they exclude ARM targets, concurrent request latency,
and disk-level write amplification.
