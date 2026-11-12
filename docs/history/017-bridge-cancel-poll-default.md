# Device Bridge: Cancel Noticed in 1 ms, Not 25 ms

014 made the executor and the orchestrator wake on events. It left one polling
hop in place and described it as harmless: "the SocketCAN `poll` in the Device
Bridge wakes on frame arrival, so no ACK path was paying a 25 ms penalty". That
is true for an ACK, which arrives as a frame. It is not true for a cancel,
which arrives as an action cancel request and produces no frame at all.

`receive_poll_ms` is the slice the bridge waits for a frame before it re-checks
`goal_handle->is_canceling()`. Nothing wakes it when the cancel request arrives,
so a cancel waits for the rest of the current slice before the STOP is built and
sent. At the old default of 25 ms that was the dominant term in
cancel-to-STOP-sent.

## What changed

The default of `receive_poll_ms` is 1 ms instead of 25 ms, in the parameter
declaration, the member initialiser, and `device_bridge.yaml`.

Nothing else changed. A frame arrival still wakes the wait immediately, so the
ACK path is unaffected: the slice only limits how long an *un*answered wait
burns before it looks at the cancel flag. The cost is one extra `poll` wakeup
per millisecond while the bridge is waiting, which is idle CPU on the order of
a few tenths of a percent, not a per-command cost.

## Measurement

Cancel request to STOP frame on the bus, 40 samples each, same host, same
`virtual_can_device` in `drop_ack` mode so the bridge is genuinely waiting when
the cancel arrives. Transport is the review harness's AF_UNIX stand-in, not
SocketCAN, so this is a software-path number.

| `receive_poll_ms` | p50 | p95 | max |
| --- | --- | --- | --- |
| 25 (old default) | 12.59 ms | 24.39 ms | 24.85 ms |
| 1 (new default) | 0.96 ms | 1.34 ms | 1.52 ms |

With no parameter override at all, so the shipped default is the one under
test: p50 0.81 ms, p95 1.64 ms, max 2.49 ms, and the bridge logs
`receive_poll_ms=1` at startup.

This is not a P50/P95/P99 of cancel request → STOP ACK, and it is not hardware
evidence. It measures how long the bridge takes to put the STOP on the wire.

## What this does not fix

The bridge still polls. The complete fix is an eventfd in the `poll` set that
`handle_cancel` writes to, which removes the slice instead of shrinking it. That
is a larger change to `runtime_can::SocketCan`'s interface and is not made here.

The default also does not shorten the STOP acknowledgement wait or the retry
path; those are bounded by `stop_timeout_ms`, not by this parameter.

## Verification

- `ear-review-1112/experiments.py e7` and `e10` on the built node (see the
  table above). `e10` is the no-override case.
- `e1`, `e5`, `e8` re-run on the same build to check that the tighter slice did
  not disturb the STOP path: identical outcomes to the 25 ms build, including
  the bridge's stop-id and shutdown behaviour on `main`.
- `colcon test --packages-select device_bridge` (`test_command_policy` 7 cases,
  `test_diagnostic_state`), plus `uncrustify` and `cppcheck`.
