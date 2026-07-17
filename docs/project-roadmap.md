# Project Roadmap

Each milestone has one purpose, a focused reference-reading task, and a
commit-ready acceptance condition.

## M0: Environment and repository

Learn: ROS 2 workspace layout, `colcon`, `rosdep`, and package manifests.

Deliver:

- Ubuntu 24.04 and ROS 2 Jazzy development environment.
- This repository inside `~/embodied_ws/src`.
- `task_contract` and `task_guard` building successfully.

Accept: `bash scripts/build_phase_1.sh` exits successfully.

## M1: Contract and semantic guard

Learn: JSON Schema and the minimal ROS 2 Action client/server lifecycle.

Deliver:

- JSON-to-`TaskRequest` adapter with schema validation.
- C++ policy loading from `task_guard/config/task_policy.yaml`.
- More Guard tests for invalid JSON, contract versions, and localization state.

Accept: every invalid request is rejected before a navigation Goal can exist.

## M2: Outer task Action and fake navigation

Learn: `rclcpp_action` goal acceptance, feedback, result, and cancellation.

Deliver:

- `task_executor` provides `ExecuteTask` as an Action Server.
- A deterministic fake `NavigateToPose` Action Server for tests.
- Outer cancellation propagates to the inner Goal.

Accept: success, rejection, feedback, client cancellation, and timeout all
produce a deterministic result code.

## M3: State machine and bounded recovery

Learn: explicit task-state transitions and BehaviorTree.CPP Retry/Fallback/
Timeout semantics.

Deliver:

- Global deadline that does not reset on retry.
- Reviewed static recovery tree with a bounded attempt count.
- `SAFE_STOP` handling after recovery or cancellation failure.

Accept: failed navigation never becomes an unbounded retry loop and never
silently starts a new task.

## M4: Nav2 and TurtleBot3 simulation

Learn: `NavigateToPose` feedback/result and Nav2 recovery behavior.

Deliver:

- Reproducible map, launch file, named targets, and one keepout zone.
- Target-pose mapping loaded only from version-controlled configuration.
- RViz markers for targets and the restricted area.

Accept: `dock`, `workbench`, and `home` can be reached in simulation; invalid
targets and keepout requests are refused by the Runtime.

## M5: Gateway and observability

Learn: structured tool-call normalization, not autonomous planning.

Deliver:

- Minimal `agent_gateway` that converts a model response to the JSON contract.
- `TaskEvent` publisher, diagnostics, Foxglove visualization, and rosbag demo.
- Optional read-only SocketCAN heartbeat input for hardware readiness.

Current: model profiles, fixed intent evaluation, live Runtime diagnostics, and
the SocketCAN readiness gate are implemented. TaskEvent, Foxglove, and rosbag
evidence remain.

Current: provider profiles, fixed intent evaluation, TaskEvent, and a
deterministic rosbag2/MCAP persistence audit are implemented. Diagnostics and
Foxglove evidence remain on their independent branches or roadmap.

Accept: changing the model provider does not modify Guard, executor, or Nav2
code.

## M6: Regression suite and release quality

Deliver:

- Twenty fixed fault and success scenarios.
- Fast fake-Action tests and a smaller TurtleBot3/Nav2 simulation suite.
- CI, formatting, linting, README demo, architecture diagram, and error table.

Current: five packages and 77 tests are in the local release gate. The full
Nav2 simulation matrix and repeated clean-run evidence remain.

Accept: all tests pass repeatedly from a clean workspace and the README gives
another developer a reproducible demo path.
