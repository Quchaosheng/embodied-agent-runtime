# Continuous Performance Regression Suite

The runtime already recorded stage events, but nothing turned them into a
baseline, so every optimization had to be argued from a one-off local
measurement. `scripts/performance_regression.py` now derives the percentiles
from that same capture and compares them with a recorded baseline.

No new tracing, no new format: every input is a `RuntimeEvent` v2 line the
runtime already writes.

## What the capture now covers

The suite needs marks at both ends of each hop, so three were added to the
existing stage log rather than to a new one:

| interval | from | to |
| --- | --- | --- |
| `gateway_admission` | own `duration_ns` on `gateway_admission` | — |
| `dispatch_ms` | `gateway_dispatch` | `action_receive` |
| `ack_ms` | `can_frame_sent` | `can_ack_received` |
| `cancel_detect_ms` | `cancel_requested` | `can_stop_sent` |
| `stop_ack_ms` | `can_stop_sent` | `can_stop_acked` |
| `cancel_total_ms` | `cancel_requested` | `can_stop_acked` |

The gateway previously wrote no stage events at all, so admission and dispatch
had no evidence to measure. It now writes `gateway_admission` and
`gateway_dispatch` with `--ros-args -p stage_event_log_path:=...`, like the
executor and the bridge already did.

A retried command emits one `can_frame_sent` per attempt. The suite pairs the
**last** attempt at or before the ACK, so a retry is not reported as a latency
that includes the earlier attempt.

## Trend only, by default — the decision this issue asked for

The acceptance list asks to either fail on a threshold or publish trend only,
and to pick one explicitly. **Default: trend only.**

The industrial E2E writes `$EVIDENCE/performance.json` and records the result
alongside the other evidence; a regression is printed and stored, not treated as
a build failure. `PERFORMANCE_REGRESSION_GATE=1` turns it into a failing
threshold when a fixed machine and a recorded baseline exist.

The reason is the one that made the earlier rounds stop short: a threshold
taken from one laptop under an unknown load fires on noise, and a guard that
cries wolf is worse than a report nobody gates on. The suite still exits
non-zero on a regression — the E2E chooses whether that is fatal.

## Coverage against the acceptance list

| Scenario | Status |
| --- | --- |
| normal run | covered — `dispatch_ms`, `ack_ms` from the `normal` scenario |
| dropped ACK / ACK timeout | covered — `ack_timeout` and `drop_stop_ack` scenarios |
| cancel | covered — `cancel_detect_ms`, `stop_ack_ms`, `cancel_total_ms` |
| growing history table | **not covered.** The E2E runs six tasks; it does not grow history to a size where SQLite query time matters. `benchmark_stats_indexes.py` covers that separately, at 1k/10k rows |
| camera enabled | **not covered.** The E2E does not start the perception adapter |

CPU and peak RSS are recorded next to the latencies by reading
`/proc/<pid>/stat` and `VmHWM`, both process-lifetime high-water marks. The CPU
figure is therefore **process lifetime total, not per request**, and is labelled
that way in the output.

## Fixed machine and stated configuration

`--label` is written into the report and the E2E passes `uname -srm` plus the
load average. Every published number is expected to carry that label; a figure
without it is not comparable.

## Verification

`test_performance_regression` (11 cases, registered in `runtime_history`'s
CTest set) pins the interval derivation, the retry pairing, the
`gateway_admission` duration path, the tolerance verdict, the missing-baseline
refusal, and the CPU/RSS read.

The E2E hook was **not executed end to end on this host**: the container has no
`vcan0` (`/proc/net/can` does not exist and the module cannot be loaded without
root), so `run_industrial_e2e.sh` cannot run here. The script passes `bash -n`
and the report path it calls is covered by the 11 unit cases, but the first real
execution of the combined run is CI's `vcan0` step or a host with the module
available. That limitation is stated rather than papered over.
