# Event-Driven Cancel and Deadline Waits

The executor and the orchestrator both used fixed sleeps to notice cancellation:
the executor polled the child state every 25 ms, the orchestrator slept 20 ms
after every tree tick. Cancel detection therefore had a bounded, avoidable
delay on top of the actual cancel path.

Both waits are now event driven where a notification exists, with explicit
deadlines where none does.

| wait | before | after |
| --- | --- | --- |
| executor: child goal response | `wait_for(25 ms)` loop | woken by the goal response callback |
| executor: cancel request | polled every 25 ms | woken by `handle_cancel` |
| executor: shutdown | polled every 25 ms | woken by `request_shutdown` |
| executor: deadline | 25 ms poll | `wait_until(deadline)` |
| executor: unconfirmed cancel retry | 25 ms poll | 20 ms retry interval, bounded by the drain deadline |
| orchestrator: child result | 20 ms tick | woken by the result callback |
| orchestrator: cancel | 20 ms tick | woken by `handle_cancel` |
| orchestrator: shutdown | 20 ms tick | woken by `request_stop` |
| orchestrator: tree state | 20 ms tick | heartbeat, configurable `tick_interval_ms` |

`rclcpp_action` publishes no event for a cancel request. The `handle_cancel`
callback is the notification: it runs on the executor thread the moment the
request arrives, and both nodes now use it to wake their worker instead of
polling for it.

Two things did not become purely event driven, and are not described as if they
had:

- **The orchestrator keeps a tick.** A `BT::StatefulActionNode` (`RetryReady`)
  only advances when it is ticked again, so the tree needs a heartbeat. The
  heartbeat no longer gates the cancel path — a cancel wakes the loop
  immediately — and it is now a parameter instead of a hard-coded constant.
- **The executor still has timed wakes.** The task deadline, the cancel drain
  bound, and the retry of a cancel dispatch that threw all need a timer. They
  are real deadlines, not a fixed cadence.

Also unchanged: the SocketCAN `poll` in the Device Bridge wakes on frame
arrival, so no ACK path was paying a 25 ms penalty to begin with. This change
only affects cancel, shutdown and deadline detection.

## Measurement

`CancelReachesTheChildWithoutWaitingForAPollTick` in `test_executor_safety`
measures cancel request to child cancel dispatch. The threshold is 15 ms, and
the test passed on 5 consecutive runs on this host (Ubuntu 24.04, x86_64, ROS 2
Jazzy), each run taking ~175 ms end to end.

This is not a P50/P95/P99 of cancel request → STOP sent → STOP ACK, and no such
percentiles are reported here. Producing them needs the industrial `vcan0` E2E
scenario plus `scripts/stage_latency_report.py` over its stage log; that run is
not part of this change, and the software `vcan0` path is not hardware evidence
in any case.

The numbers from different layers are not additive and are not presented as a
total latency improvement.

Verification: `colcon test --packages-select task_executor task_orchestrator`
(`test_executor_safety` 22 GoogleTest cases, `test_orchestrator_faults` 6,
`test_ready_state` 5, plus the two launch tests).
