# Idempotency Retention Off the Admission Path

`DurableIdempotency::admit()` used to run `prune_idempotency()` before every
first-time dispatch. That delete scans the whole ledger, and the gateway
admission path shares the `Store` mutex with `stats()`, so every admission paid
for a full retention scan whether or not anything was reclaimable.

Retention now runs on a `ledger_prune_period_ms` timer (default 60 s) owned by
the gateway node. Admission keeps the part it needs: a bounded
`count_idempotency()` over the unexpired window, which is what enforces the
ledger bound. Admission ordering is unchanged — the durable `ADMITTED` row is
still written before dispatch.

Behaviour change worth stating: an expired identity now stays replayable until
the next maintenance pass instead of disappearing on the next admission. That
direction is safe. An expired row is past the retry horizon, so it can only be
replayed as history that would otherwise have been deleted, and capacity is
measured over the unexpired window, so a lingering expired row does not lock
out a new request. It can never make a request re-dispatch earlier.

Measured with `scripts/benchmark_stats_indexes.py` on this host (Ubuntu 24.04,
x86_64, `-O2`, system SQLite, `PRAGMA synchronous=NORMAL`, 6 samples, median):

| rows | admission ms | admission ms during `stats()`, p50/p95 | retention scan ms |
| ---: | ---: | ---: | ---: |
| 1,000 | 0.061 | 0.085 / 0.109 | 1.81 |
| 10,000 | 0.277 | 0.410 / 2.183 | 19.48 |

The retention column is the work that used to run inside every admission. The
contention column is a proxy, not the runtime's own mutex: two SQLite
connections on one file, one looping `stats()` while the other measures the
admission read/write pair. It bounds the direction of the effect, not the
runtime's exact wait.

These figures are one host and one configuration. They exclude ARM targets,
disk-level write amplification, and admission latency under a real concurrent
RPC load.

Verification: `colcon test --packages-select runtime_gateway` (`test_durable_idempotency`
11 GoogleTest cases, including the retention-timing test) and
`python3 scripts/benchmark_stats_indexes.py --contention` on this host.
