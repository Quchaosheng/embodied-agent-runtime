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
- Fake navigation server, launch tests, and two process smoke tests.
- Fake, official OpenAI, and OpenAI-compatible relay provider profiles.
- Twenty-case Chinese intent evaluation and no-ROS provider probe.
- ROS 2 Jazzy GitHub Actions workflow and release verification gate.
- Third-party dependency and license inventory without vendored source trees.
- CI rosdep initialization and bounded update retry for clean GitHub runners.
- GitHub-native SVG hero, trust-boundary architecture, fail-closed flow, state
  machine, and project status badges.
- Live TF/Nav2 readiness, atomic single-task ownership, and standard ROS diagnostics.
- Launch tests for BUSY rejection, missing localization, missing navigation, and readiness recovery.
- Read-only Linux SocketCAN heartbeat bridge with strict versioned frame parsing,
  monotonic timeout, diagnostics, and optional Runtime gating.
- Real `PF_CAN` vcan smoke proof for missing, ready, and stale heartbeat states.

### Not Yet Released

- Real Nav2/TurtleBot3 simulation evidence.
- Live OpenAI or relay evaluation evidence.
- TaskEvent diagnostics, Foxglove, rosbag, and physical controller command/ACK integration.
