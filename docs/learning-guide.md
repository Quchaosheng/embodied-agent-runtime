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
failures. The next proof is version-controlled target poses and real Nav2. Do
not let the model provide coordinates.

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

Read the TurtleBot3 launch files in `reading-map.md` and start the selected
simulation world.

Build next: one launch command for the Runtime plus Nav2 and TurtleBot3.
Replace the placeholder target poses only after inspecting the simulation map.

Proof: `dock`, `workbench`, and `home` succeed repeatedly. RViz displays each
target, the current Goal, and the restricted area.

## Session 6: Gateway Protocol

Read the Qwen-Agent function-calling files in `reading-map.md`.

Build next: a minimal Gateway adapter that returns only the task JSON Schema.
Use a fixed JSON fixture first, then one model provider.

Proof: changing the Gateway implementation does not modify any C++ Guard,
executor, Nav2, or simulation file.

## Session 7: Observability and Regression

Implemented: publish reliable transient-local `TaskEvent` transitions and
verify complete lifecycle sequences plus late-subscriber history.

Build next: collect ROS diagnostics, open TaskEvent and health data in
Foxglove, and record one rosbag.

Proof: the test suite contains twenty fixed scenarios and the README contains
one success trace plus one timeout-to-safe-stop trace.

## Working rhythm

For every session:

1. Read only the named files.
2. Explain the observed lifecycle or policy in one short note.
3. Make one narrow implementation change.
4. Run the smallest relevant test.
5. Review the diff and create one focused Git commit.

This rhythm keeps the project understandable and prevents the model, Nav2,
simulation, and recovery logic from becoming one untraceable failure surface.
