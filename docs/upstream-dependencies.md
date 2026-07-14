# Upstream Dependencies

This project composes upstream projects through their public APIs. It does not
copy their source into this repository.

| Upstream project | Role in this project | Integration boundary |
| --- | --- | --- |
| Navigation2 | Navigation executor | `nav2_msgs/action/NavigateToPose` Action Client |
| BehaviorTree.CPP | Static task-level recovery | Predefined, reviewed recovery tree only |
| ROS 2 examples | Learning reference | No runtime dependency |
| Qwen-Agent | Tool-calling protocol reference | No runtime dependency and no framework import |
| TurtleBot3 | Reproducible robot model | Simulation dependency |
| TurtleBot3 Simulations | Nav2 simulation environment | Launch and map dependency |

## Planned runtime dependencies

- ROS 2: `rclcpp`, `rclcpp_action`, `rclcpp_lifecycle`, `nav2_msgs`,
  `geometry_msgs`, `tf2_geometry_msgs`, and `diagnostic_updater`.
- C++: `nlohmann_json`, `yaml-cpp`, and BehaviorTree.CPP.
- Verification and presentation: `ament_cmake_gtest`, `launch_testing`,
  `rviz2`, `visualization_msgs`, `foxglove_bridge`, and `rosbag2`.
- Optional device-readiness extension: Linux SocketCAN and `can-utils` for a
  `vcan0` test bus. This is introduced only after the core Runtime is stable.

## Version policy

The first supported environment will be Ubuntu 24.04 with ROS 2 Jazzy. Before
the first Nav2 build, a `deps.repos` file will pin the matching upstream
branches or commits. Reference repositories currently downloaded in the parent
directory must not be treated as a compatible build set until then.
