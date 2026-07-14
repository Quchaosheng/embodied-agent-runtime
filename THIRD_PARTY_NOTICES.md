# Third-Party Dependencies and Acknowledgements

This repository composes third-party software through installed packages and
public APIs. It does not vendor or copy the upstream source trees listed below.
`rosdep`, Ubuntu packages, and ROS 2 packages install them outside this
repository. Each dependency remains governed by its own license.

This notice is an engineering inventory, not a replacement for the upstream
license texts. A binary or container release must review the exact transitive
packages included in that artifact.

## Direct Runtime and Build Dependencies

| Project | Use in this repository | Upstream license |
| --- | --- | --- |
| [ROS 2 Jazzy](https://docs.ros.org/en/jazzy/) | Nodes, Actions, geometry messages, interfaces, ament build, package discovery | Predominantly Apache-2.0; check each ROS package |
| [Navigation2](https://github.com/ros-navigation/navigation2) | `nav2_msgs/action/NavigateToPose` interface | Apache-2.0 |
| [TurtleBot3](https://github.com/ROBOTIS-GIT/turtlebot3) | Burger model, matching navigation map and Nav2 parameters | Apache-2.0 |
| [TurtleBot3 Simulations](https://github.com/ROBOTIS-GIT/turtlebot3_simulations) | Gazebo Sim world, robot spawning and ROS/Gazebo bridge configuration | Apache-2.0 |
| [Gazebo Sim / ros_gz](https://github.com/gazebosim) | Physics simulation and ROS 2 transport bridge | Apache-2.0 and dependency-specific licenses |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | Guard policy and named-target YAML parsing | MIT/X11 |
| [python-jsonschema](https://github.com/python-jsonschema/jsonschema) | Draft 7 validation at the model-output boundary | MIT |
| Python standard library | HTTP transport, JSON parsing, CLI, data types | Python Software Foundation License |

## Test and Development Dependencies

| Project | Use in this repository | Upstream license |
| --- | --- | --- |
| [GoogleTest](https://github.com/google/googletest) | C++ Guard and target-map unit tests | BSD-3-Clause |
| [pytest](https://github.com/pytest-dev/pytest) | Python Gateway and Provider tests | MIT |
| ROS 2 `launch_testing` | Multi-process Action lifecycle tests | Apache-2.0 |
| [colcon](https://colcon.readthedocs.io/) and `rosdep` | Workspace build and dependency installation | Check the installed package metadata |
| [ros-tooling/setup-ros](https://github.com/ros-tooling/setup-ros) | GitHub Actions ROS environment | Apache-2.0 |

## Protocol and Service Compatibility

The OpenAI-compatible Provider implements an HTTP/tool-calling protocol using
the Python standard library. No OpenAI SDK or OpenAI source code is copied into
this repository. Using OpenAI or a relay is also subject to that service's
terms, privacy policy, and model availability; it is not an open-source runtime
dependency bundled with this project.

## Planned or Reference-Only Projects

The following names appear in roadmap or learning documents but their source is
not imported and they are not current runtime dependencies:

- [BehaviorTree.CPP](https://github.com/BehaviorTree/BehaviorTree.CPP): planned
  only if reviewed recovery actions outgrow the current bounded C++ loop.
- Foxglove bridge and rosbag2: planned observability and replay evidence.
- Qwen-Agent and ROS 2 examples: reading references only.

Before any planned dependency becomes runtime code, add it to the appropriate
`package.xml`, install it through `rosdep` or a pinned source manifest, verify
version compatibility, and update this notice with its exact license.
