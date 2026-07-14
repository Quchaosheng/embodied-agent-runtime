# Task Executor

The package now contains the first M2 runtime slice:

- execute_task_server exposes the outer ExecuteTask Action.
- It validates the Goal with task_guard before creating an inner Goal.
- It loads named targets from version-controlled YAML and converts yaw to a
  map-frame pose quaternion.
- It forwards NavigateToPose feedback to ExecuteTask feedback.
- It propagates outer cancellation to the inner Goal with bounded confirmation.
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
- both test processes exit cleanly.

Run it with:

    colcon test --packages-select task_executor
    colcon test-result --verbose

System integration status:

- `runtime_simulation` now launches this server against a real Nav2 stack and
  provides a headless two-Goal smoke; local execution still needs the full
  TurtleBot3/Nav2 packages installed.
- ROS diagnostics, persistent rosbag evidence, and reviewed recovery actions
  beyond the minimal fixed retry loop remain future work.

No model-facing code belongs in this package.
