# Final Demo Specification

The finished repository should demonstrate a safety-bounded task runtime, not
a conversational robot assistant.

## What runs

```text
agent_gateway
  -> /execute_task (ExecuteTask Action)
  -> task_guard
  -> task_executor
  -> /navigate_to_pose (Nav2 Action)
  -> TurtleBot3 simulation
```

The Runtime publishes `TaskEvent` and diagnostics. RViz shows the robot, named
targets, restricted area, and current Goal. Foxglove shows task events,
feedback, terminal states, and error codes.

## Four required demonstrations

### 1. Successful allowed task

Input:

```json
{"contract_version":1,"action":"navigate","target":"dock","deadline_s":90}
```

Expected: target validates, Nav2 Goal is dispatched, feedback is visible, and
the final result is `SUCCEEDED`.

### 2. Rejected unsafe request

Input: an unknown target, extra field, invalid deadline, or malformed JSON.

Expected: no Nav2 Goal is sent; the result carries an explicit contract or
policy error code.

### 3. Deadline cancellation

Input: a valid request configured to exceed its global deadline.

Expected: the Runtime cancels Nav2, waits for bounded confirmation, publishes
the cancellation transition, and enters `SAFE_STOP` or `CANCELLED`.

### 4. Bounded recovery

Input: a valid task while the fake server or simulation forces navigation
failure.

Expected: exactly the configured number of attempts occurs. The Runtime ends
in `FAILED` or `SAFE_STOP`, never loops indefinitely and never invents a new
task.

## Release evidence

The final GitHub repository should include:

- A short architecture diagram and safety-boundary statement.
- One command to launch the simulation and one command to send a task.
- RViz and Foxglove screenshots or a short GIF of the successful task.
- A captured timeout/cancellation trace.
- The twenty-scenario regression matrix and CI status.
- A clear scope statement: no coordinate-generating model, no dynamic safety
  policy, no mechanical arm, no vision, no SLAM, and no multi-robot behavior.

## Definition of done

Another developer can clone the repository, follow the README on Ubuntu 24.04
with ROS 2 Jazzy, launch the simulation, run the success and failure demos,
and reproduce the tests without changing any safety policy in model prompts.
