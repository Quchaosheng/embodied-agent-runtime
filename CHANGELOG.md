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
- GitHub-native SVG hero, trust-boundary architecture, fail-closed flow, state
  machine, and project status badges.
- Live TF/Nav2 readiness, atomic single-task ownership, and standard ROS diagnostics.
- Launch tests for BUSY rejection, missing localization, missing navigation, and readiness recovery.
- Read-only Linux SocketCAN heartbeat bridge with strict versioned frame parsing,
  monotonic timeout, diagnostics, and optional Runtime gating.
- Real `PF_CAN` vcan smoke proof for missing, ready, and stale heartbeat states.
- Reliable transient-local TaskEvent transitions and late-subscriber lifecycle
  verification across success, rejection, cancellation, deadline, and recovery.
- Rosbag2/MCAP persistence and deterministic audit of successful and rejected
  TaskEvent timelines.
- Strict 1-3 step MissionPlan validation, a bounded MissionModel boundary, and
  serial MissionRunner execution through the existing ExecuteTask Action.
- Fixed mission evaluation, a no-motion mission probe, and offline mission smoke.
- Reproducible TurtleBot3 Burger, Gazebo Sim, AMCL, Nav2, Runtime, and RViz launch.
- Local Nav2/TurtleBot3 evidence for an ordered `dock -> workbench` mission and
  lifecycle checking that rejects `inactive` as a false positive.

### Not Yet Released

- Enforced Nav2 keepout costmap filtering; the current restricted-area polygon is visualization-only.
- Live OpenAI or relay evaluation evidence.
- Foxglove visualization and physical controller command/ACK integration.
