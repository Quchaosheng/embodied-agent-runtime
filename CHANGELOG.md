# Changelog

All notable project changes are recorded here. The project currently uses a
single pre-release line while Nav2/TurtleBot3 system evidence remains open.

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
- Reproducible TurtleBot3 Burger, Gazebo Sim, AMCL, Nav2, Runtime, and RViz launch.
- Reviewed free-space named targets, bounded initial-pose publication, scene markers,
  and a two-Goal headless Nav2 system smoke script.

### Not Yet Released

- Local real Nav2/TurtleBot3 smoke evidence after installing the system packages.
- Enforced Nav2 keepout costmap filter; the current polygon is visualization-only.
- Live OpenAI or relay evaluation evidence.
- TaskEvent diagnostics, Foxglove, rosbag, and hardware integration.
