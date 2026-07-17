# Upstream Dependencies

This project composes upstream projects through installed packages and public
APIs. It does not copy their source into this repository. Direct dependency
and license acknowledgements are recorded in `THIRD_PARTY_NOTICES.md`.

## Implemented dependencies

| Upstream project | Current role | Integration boundary |
| --- | --- | --- |
| Navigation2 | Navigation executor | `nav2_msgs/action/NavigateToPose` Action Client |
| TurtleBot3 and TurtleBot3 Simulations | Robot model, navigation configuration, and Gazebo world | Installed ROS packages composed by `runtime_simulation` |
| Gazebo Sim and ros_gz | Physics simulation and ROS 2 bridge | Installed ROS packages used by the Nav2 system smoke |
| ROS 2 Jazzy | Nodes, Actions, interfaces, package discovery | `rclcpp`, `rclpy`, ament, rosidl |
| Linux SocketCAN | Read-only controller-heartbeat input | `PF_CAN` raw socket on `vcan0` or physical CAN interface |
| can-utils | Reproducible virtual-CAN stimulus | `cansend` used only by the vcan smoke script |
| yaml-cpp | Reviewed policy and target configuration | Linked C++ library |
| python-jsonschema | Model-output Schema validation | Installed Python package |
| rosbag2 and MCAP storage | Persisted TaskEvent audit evidence | ROS 2 CLI and `rosbag2_py` API |
| GoogleTest, pytest, launch_testing | Unit and process verification | Test-only dependencies |

All current direct dependencies are declared in the package manifests and are
installed by `rosdep`; their source repositories are not Git submodules.

## Planned or reference-only projects

- BehaviorTree.CPP: optional reviewed recovery workflow; not currently linked.
- Foxglove bridge: planned visualization evidence.
- Qwen-Agent and ROS 2 examples: learning references only; no framework import.

## Version policy

The supported environment is Ubuntu 24.04 with ROS 2 Jazzy. Prefer ROS binary
packages resolved by `rosdep`. Add a pinned `deps.repos` file only when a
required dependency cannot be supplied by the supported ROS distribution.
Reference repositories outside this project must not be treated as part of the
build or uploaded with this repository.
