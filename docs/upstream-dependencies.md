# Upstream Dependencies

This project composes upstream projects through installed packages and public
APIs. It does not copy their source into this repository. Direct dependency
and license acknowledgements are recorded in `THIRD_PARTY_NOTICES.md`.

## Implemented dependencies

| Upstream project | Current role | Integration boundary |
| --- | --- | --- |
| Navigation2 | Navigation executor | `nav2_msgs/action/NavigateToPose` Action Client |
| TurtleBot3 | Burger model, static map, and Nav2 parameters | ROS 2 Jazzy binary packages |
| TurtleBot3 Simulations and Gazebo Sim | Physics world and ROS bridge | `runtime_simulation` launch composition |
| ROS 2 Jazzy | Nodes, Actions, interfaces, package discovery | `rclcpp`, `rclpy`, ament, rosidl |
| yaml-cpp | Reviewed policy and target configuration | Linked C++ library |
| python-jsonschema | Model-output Schema validation | Installed Python package |
| GoogleTest, pytest, launch_testing | Unit and process verification | Test-only dependencies |

All current direct dependencies are declared in the package manifests and are
installed by `rosdep`; their source repositories are not Git submodules.

## Planned or reference-only projects

- BehaviorTree.CPP: optional reviewed recovery workflow; not currently linked.
- Nav2 keepout costmap filter: the current restricted polygon is an unenforced
  RViz marker only.
- Foxglove bridge and rosbag2: planned observability evidence.
- Qwen-Agent and ROS 2 examples: learning references only; no framework import.
- Optional device-readiness extension: Linux SocketCAN and `can-utils` for a
  `vcan0` test bus. This is introduced only after the core Runtime is stable.

## Version policy

The supported environment is Ubuntu 24.04 with ROS 2 Jazzy. Prefer ROS binary
packages resolved by `rosdep`. Add a pinned `deps.repos` file only when a
required dependency cannot be supplied by the supported ROS distribution.
Reference repositories outside this project must not be treated as part of the
build or uploaded with this repository.
