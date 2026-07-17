# Learning Guide

This project is learned by building a small, verified artifact after every
reading task. Do not read whole repositories and do not add a new subsystem
until the current artifact has a test or an observable demo.

## Session 1: ROS 2 Actions

Read the four `ros2/examples` files listed in `reading-map.md`.

Answer these questions in your own notes:

1. When can an Action Server reject a Goal?
2. Where does an Action Client receive feedback?
3. How does a client request cancellation and observe the final result?

The first fake navigation server and minimal `ExecuteTask` Action Server are
now implemented. Reproduce their process-level proof with
`scripts/smoke_phase_2.sh`. The automated `launch_testing` fixture now covers
success, feedback, rejection, cancellation, global deadline, and process exit.
Bounded retry now reuses the original deadline and ends in SAFE_STOP after two
failures. Version-controlled target poses and real Nav2 are also verified; the
model still cannot provide coordinates.

Proof: an automated test covers accepted Goal, feedback, success, and client
cancellation.

## Session 2: Task Contract and Guard

Read `task_contract/schema/task_request.schema.json`,
`task_contract/action/ExecuteTask.action`, and the Guard tests.

Build next: JSON parsing at the Gateway boundary and policy-file loading in
`task_guard`.

Proof: malformed JSON, an unknown target, deadline 91, busy robot state, and
missing localization are all rejected with distinct error codes.

## Session 3: Nav2 as an Executor

Read the Nav2 files listed in `reading-map.md`. Trace where a `NavigateToPose`
Goal starts, where feedback is produced, and how a terminal result is handled.

Build next: replace the fake navigation client only with a
`nav2_msgs/action/NavigateToPose` Action Client. Keep the outer `ExecuteTask`
Action unchanged.

Proof: the Runtime forwards distance feedback and propagates cancellation from
the outer task to Nav2.

## Session 4: Static Recovery

Read BehaviorTree.CPP `Fallback`, `Retry`, and `Timeout` sources and tests.

The first predefined recovery flow is implemented directly in C++: navigate,
retry once after an accepted Goal fails, then report `SAFE_STOP`. A richer
BehaviorTree.CPP flow remains optional until a real recovery action is needed.

Proof: a fake Nav2 server that aborts twice produces exactly two attempts and
`RECOVERY_EXHAUSTED`. The original task deadline is not reset on retry.

## Session 5: TurtleBot3 Simulation

Read the TurtleBot3 launch files in `reading-map.md`, then compare them with
`simulation/launch/runtime_nav2_sim.launch.py`. Trace how world, map, spawn
pose, AMCL initial pose, simulated time, and Nav2 lifecycle fit together.

Proof: run `scripts/smoke_nav2_sim.sh` and explain why its two real-Nav2 Goals
complement rather than replace the deterministic fake-Action tests. RViz must
display each target and label the current restricted polygon as unenforced.
The local 2026-07-17 evidence is `bt_navigator` active followed by successful
`home -> dock -> workbench` outer Runtime Goals.

## Session 6: Gateway Protocol

Read the Qwen-Agent function-calling files in `reading-map.md`.

Build next: a minimal Gateway adapter that returns only the task JSON Schema.
Use a fixed JSON fixture first, then one model provider.

Proof: changing the Gateway implementation does not modify any C++ Guard,
executor, Nav2, or simulation file.

## Session 7: Observability and Regression

Runtime readiness diagnostics are implemented from the same TF, Nav2, and BUSY
facts used by the Guard. Reliable transient-local `TaskEvent` transitions and
their rosbag2/MCAP audit are also implemented; read lessons 18 through 20.

Build next: open diagnostics and events in Foxglove, then record a combined
health and task-event demonstration.

Proof: the test suite contains twenty fixed scenarios and the README contains
one success trace plus one timeout-to-safe-stop trace.

## Session 18: Bounded AI Mission Agent

Read `learning-session-18-bounded-ai-mission-agent.zh-CN.md`, then trace
`MissionModel.plan() -> MissionRunner -> ExecuteTask -> MissionModel.decide()`.
Explain why AI participates at planning, checkpoint, and summary stages while
coordinates and every ROS Goal remain outside the model boundary.

Proof: the Fake model passes 12/12 fixed mission cases with zero unsafe
acceptances. The offline mission smoke completes two ExecuteTask steps, and the
opt-in system smoke completes `dock -> workbench` through real local Nav2.
Provider failure, an unavailable transition, or an expired mission budget does
not create the next Goal.

## Working rhythm

For every session:

1. Read only the named files.
2. Explain the observed lifecycle or policy in one short note.
3. Make one narrow implementation change.
4. Run the smallest relevant test.
5. Review the diff and create one focused Git commit.

This rhythm keeps the project understandable and prevents the model, Nav2,
simulation, and recovery logic from becoming one untraceable failure surface.
