# Changelog

All notable project changes are recorded here. The project currently uses a
single pre-release line while the real Nav2/TurtleBot3 milestone remains open.

## [Unreleased]

### Added

- Strict JSON TaskRequest contract and duplicate-key rejection.
- C++ Guard with version-controlled deployment policy.
- Outer ExecuteTask and inner NavigateToPose Action lifecycle.
- Confirmed cancellation, global deadline, bounded retry, and SAFE_STOP.
- Fail-closed named-target YAML loading and yaw conversion.
- Fake navigation server, launch tests, and three process smoke tests.
- Fake, official OpenAI, and OpenAI-compatible relay provider profiles.
- Twenty-case Chinese intent evaluation and no-ROS provider probe.
- ROS 2 Jazzy GitHub Actions workflow and release verification gate.
- Third-party dependency and license inventory without vendored source trees.
- CI rosdep initialization and bounded update retry for clean GitHub runners.
- Reliable transient-local TaskEvent transitions and late-subscriber lifecycle
  verification across success, rejection, cancellation, deadline, and recovery.
- Rosbag2/MCAP persistence and deterministic audit of successful and rejected
  TaskEvent timelines.

### Not Yet Released

- Real Nav2/TurtleBot3 simulation evidence.
- Live OpenAI or relay evaluation evidence.
- ROS diagnostics, Foxglove, and hardware integration.
