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

- Reproducible TurtleBot3/Gazebo/Nav2 launch, matching map, and named targets.
- Target-pose mapping loaded only from version-controlled configuration.
- RViz markers for targets and the restricted area.

Complete: launch composition, AMCL initialization, reviewed target poses, RViz
markers, and a two-Goal headless smoke are implemented. On 2026-07-17 Nav2
reached active and both `home -> dock` and `dock -> workbench` succeeded through
the outer `ExecuteTask` Runtime in Gazebo Sim.

Accept: `dock`, `workbench`, and `home` can be reached in simulation. Keepout
acceptance remains separate until a Nav2 costmap filter and refusal test exist.

## M5: Gateway and bounded AI mission

Learn: structured tool-call normalization and constrained task-level agency.

Deliver:

- Minimal `agent_gateway` that converts a model response to the JSON contract.
- Strict 1-3 step MissionPlan and serial MissionRunner.
- Runtime-offered checkpoint transitions and read-only summaries.
- Fake, official OpenAI, and OpenAI-compatible relay profiles.

Complete: 20/20 single-task cases, 12/12 mission cases with zero unsafe
acceptances, offline process smoke, and a Fake-model mission through real local
Nav2. Changing the model provider does not modify Guard, executor, or Nav2.

## M6: Regression suite and release quality

Deliver:

- Twenty fixed fault and success scenarios.
- Twelve fixed bounded mission scenarios.
- Fast fake-Action tests and a smaller TurtleBot3/Nav2 simulation suite.
- CI, formatting, linting, README demo, architecture diagram, and error table.
- `TaskEvent` publisher, diagnostics, Foxglove visualization, and rosbag demo.

In progress: the release gate, CI, and reviewer-facing evidence exist;
TaskEvent/Foxglove/rosbag remain on the observability branch. Accept when all
tests pass repeatedly from a clean workspace and one task trace can be replayed.
