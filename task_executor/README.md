# Task Executor

The package now contains the first M2 runtime slice:

- execute_task_server exposes the outer ExecuteTask Action.
- It validates the Goal with task_guard before creating an inner Goal.
- It loads named targets from version-controlled YAML and converts yaw to a
  map-frame pose quaternion.
- It forwards NavigateToPose feedback to ExecuteTask feedback.
- It propagates outer cancellation to the inner Goal with bounded confirmation.
- It derives localization readiness from `map -> base_link` TF and navigation
  readiness from live `NavigateToPose` Action discovery.
- It can require a fresh `/device_ready` heartbeat and returns error 18 before
  navigation when the signal is false, missing, or stale.
- It atomically reserves one task slot and returns error 15 for an overlapping Goal.
- It publishes localization, navigation, optional device, and BUSY state on
  `/diagnostics`.
- fake_navigate_to_pose_server uses the real nav2_msgs Action type and produces
  deterministic feedback, success, and cancellation behavior.

Smoke evidence:

- dock succeeds with three feedback messages and final_state 5.
- laboratory is aborted with error_code 13 before a Nav2 server is needed.

Reproduce the smoke evidence from the workspace:

    cd ~/embodied_ws
    bash src/embodied-agent-runtime/scripts/smoke_phase_2.sh

The script starts both local Action servers, waits for `/execute_task` and
`/navigate_to_pose`, sends the two deterministic requests, checks the result
fields, and cleans up both processes. It is a process-level wiring proof, not
an alternative to automated assertions.

Automated launch evidence:

- success forwards exactly `3.0`, `2.0`, and `1.0` feedback values.
- an unknown target aborts with error 13 and zero navigation attempts.
- outer cancellation reaches an inner `CANCELED` terminal result before the
  outer Goal returns `CANCELLED`.
- a one-second global deadline cancels slow navigation and returns error 32
  with one recorded attempt.
- the executor reads `max_navigation_attempts` and cancel confirmation timing
  from the installed task_guard policy.
- one failed navigation emits RECOVERING and retries once within the original
  deadline; two failures end in SAFE_STOP with error 34.
- a second Goal is aborted with error 15 while the first owns the task slot.
- missing localization TF returns error 16 and missing Nav2 returns error 17.
- missing or stale required device heartbeat returns error 18 with zero attempts.
- diagnostics distinguish ready, localization-unavailable, navigation-unavailable,
  task-active, and explicit localization-check bypass states.
- both test processes exit cleanly.

Run it with:

    colcon test --packages-select task_executor
    colcon test-result --verbose

Not complete yet:

- connection to a running Nav2 stack and physical robot.
- hardware emergency-stop, motor watchdog, and physical controller command/ACK integration.
- reviewed recovery actions beyond the minimal fixed retry loop.

No model-facing code belongs in this package.
