# Statistics Read from a Second Connection

`GetStats` used to run its whole-table scan through `Store::stats()`, which
holds the same mutex every write on the admission path takes. A gateway that
answered `GetStats` therefore stalled new `SubmitWorkflow` calls for as long as
the scan ran: measured at 1M rows, admission p50 went from 1.2 ms to 188 ms.

The store is in WAL mode, so a reader does not have to fight the writer for the
file. What it fought was the process-local mutex. Splitting the lock was not
enough on its own: one `sqlite3` connection is a single cursor context, and
interleaving statements from two threads on it is not what SQLITE_OPEN_FULLMUTEX
promises.

## What changed

`runtime_history::Store::Reader` is a second, read-only connection to the same
file. `Store` grew a `path()` accessor so a `Reader` is pointed at the file the
writer actually opened instead of at a second copy of the path. The statistics
query itself moved into one free function both connections call, so the two
cannot drift apart.

`GatewayService` builds one `Reader` alongside the store at startup and
`GetStats` uses it. Every other call still goes through `Store`.

The reader opens with `SQLITE_OPEN_READONLY` and no `CREATE`: a mistyped
database path fails at construction instead of silently producing a fresh empty
database whose every answer is "no data".

## Measurement

The same four `Store` calls a new `SubmitWorkflow` makes on admission
(`get`, `get_idempotency`, `count_idempotency`, `put_idempotency`), timed alone
and while a statistics query runs. One statistics query is started, given 1 ms
to enter SQLite, then one admission is timed; 20 rounds per configuration.

| `task_runs` rows | `stats()` median | admission alone p50 | admission during **writer** `stats()` p50 | admission during **reader** `stats()` p50 |
| --- | --- | --- | --- | --- |
| 10,000 | 1.9 ms | 1.7 ms | 2.8 ms | 1.1 ms |
| 100,000 | 8.3 ms | 1.2 ms | 22.9 ms | 1.2 ms |
| 1,000,000 | 75.8 ms | 1.2 ms | 188.4 ms | 1.7 ms |

p99 on the same runs: 226.9 ms through the writer against 2.8 ms through the
reader at 1M rows.

Host-local x86_64/NVMe numbers from a standalone benchmark that links the
unmodified `store.cpp`; not ARM, not eMMC, not a ROS integration test. The
absolute values matter less than the column difference: the reader removes the
lock wait, and what remains is SQLite's own read cost.

## What this does not fix

- **`task_runs` still grows without bound.** The scan gets slower every day the
  runtime runs; the reader keeps that off the admission path but does not make
  statistics cheap. A retention policy is a separate change.
- **The reader is read-only but not a snapshot.** A `GetStats` that overlaps a
  commit can see the new row in the count and not in the percentile probe. That
  was already true on the writer connection (four separate statements), so this
  is not a regression, but it is not a consistent snapshot either.
- **`GetTask` still uses the writer connection.** It is a single indexed row
  read, so it does not hold the lock long enough to matter; the measurement
  above is only about `stats()`.

## Verification

- `test_store` gained three cases: the reader returns the same statistics as the
  store, the reader sees rows committed after it opened (WAL visibility), and
  the reader refuses a path that does not exist. 27 GoogleTest cases pass in
  `runtime_history`.
- `uncrustify`, `cppcheck`, `lint_cmake`, `pep257`, `xmllint` pass.
- `runtime_gateway` **was not built or tested on this host**: there is no
  protobuf or gRPC development package here (`libprotobuf-dev`,
  `libgrpc++-dev`). The gateway change is the two-line construction and the
  `GetStats` call site; it compiles-or-not in CI, not here.
- `ear-review-1112/reader_stats_bench.cpp` produced the table above.
