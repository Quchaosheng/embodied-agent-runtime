# Reading Map

Read the listed files only when entering the corresponding milestone. The goal
is to learn integration boundaries, not to reproduce upstream implementations.

## ROS 2 Action lifecycle

Repository: `ros2/examples`

- `rclcpp/actions/minimal_action_client/not_composable.cpp`
- `rclcpp/actions/minimal_action_client/not_composable_with_feedback.cpp`
- `rclcpp/actions/minimal_action_client/not_composable_with_cancel.cpp`
- `rclcpp/actions/minimal_action_server/single_goal.cpp`

Focus: Goal callbacks, feedback callbacks, result handling, and cancellation.

## Nav2 executor and recovery

Repository: `navigation2`

- `nav2_bt_navigator/src/navigators/navigate_to_pose.cpp`
- `nav2_bt_navigator/behavior_trees/navigate_to_pose_w_replanning_and_recovery.xml`
- `nav2_behavior_tree/plugins/control/recovery_node.cpp`
- `nav2_behavior_tree/plugins/action/navigate_to_pose_action.cpp`

Focus: treat Nav2 as the executor. Learn its recovery vocabulary but do not
copy its controller or planner implementation into this project.

## Static task recovery

Repository: `BehaviorTree.CPP`

- `include/behaviortree_cpp/controls/fallback_node.h`
- `src/controls/fallback_node.cpp`
- `include/behaviortree_cpp/decorators/retry_node.h`
- `src/decorators/retry_node.cpp`
- `include/behaviortree_cpp/decorators/timeout_node.h`
- `src/decorators/timeout_node.cpp`
- `tests/gtest_fallback.cpp`

Focus: static `Fallback`, bounded `Retry`, and deadline-aware `Timeout`.

## Model tool-call normalization

Repository: `Qwen-Agent`

- `qwen_agent/llm/function_calling.py`
- `examples/function_calling.py`
- `tests/llm/test_function_content.py`

Focus: structured arguments and validation. Do not import the Agent framework
or delegate safety decisions to a model.

## TurtleBot3 and Nav2 simulation

Repository: `turtlebot3`

- `turtlebot3_navigation2/launch/navigation2.launch.py`
- `turtlebot3_navigation2/param/burger.yaml`
- `turtlebot3_navigation2/map/map.yaml`

Repository: `turtlebot3_simulations`

- `turtlebot3_gazebo/launch/turtlebot3_world.launch.py`
- `turtlebot3_gazebo/launch/spawn_turtlebot3.launch.py`
- `turtlebot3_gazebo/launch/robot_state_publisher.launch.py`
- `turtlebot3_gazebo/launch/turtlebot3_house.launch.py`
- `turtlebot3_gazebo/rviz/tb3_gazebo.rviz`

Focus: a fixed, repeatable mobile-base environment. Do not add SLAM, vision,
manipulation, or multi-robot scope to the first release.
