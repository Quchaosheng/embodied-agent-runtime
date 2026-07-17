# Changelog

All notable project changes are recorded here. The project currently uses a
single pre-release line while live-model and hardware evidence remain open.

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
- Strict bounded MissionPlan, three-stage MissionModel boundary, and serial
  MissionRunner through the existing ExecuteTask Action.
- Twelve-case mission evaluation, no-motion mission probe, and offline mission smoke.
- Opt-in Fake-model mission through real local Nav2, including ordered
  `dock -> workbench` success and one bounded checkpoint decision.
- ROS 2 Jazzy GitHub Actions workflow and release verification gate.
- Third-party dependency and license inventory without vendored source trees.
- CI rosdep initialization and bounded update retry for clean GitHub runners.
- Reproducible TurtleBot3 Burger, Gazebo Sim, AMCL, Nav2, Runtime, and RViz launch.
- Reviewed free-space named targets, bounded initial-pose publication, scene markers,
  and a two-Goal headless Nav2 system smoke script.
- Local Nav2/TurtleBot3 system evidence: lifecycle active and two sequential
  outer Runtime Goals succeeded through Gazebo Sim on 2026-07-17.
- Exact lifecycle-state checking with a regression test that rejects `inactive`.
- A simulation-only collision-monitor timeout override for the 5 Hz TurtleBot3
  laser, with regression coverage for the effective Nav2 parameter.

### Not Yet Released

- Enforced Nav2 keepout costmap filter; the current polygon is visualization-only.
- Live OpenAI or relay evaluation evidence.
- TaskEvent diagnostics, Foxglove, rosbag, and hardware integration.
