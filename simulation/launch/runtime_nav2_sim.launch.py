from launch import LaunchDescription
from launch.actions import (
    AppendEnvironmentVariable,
    DeclareLaunchArgument,
    IncludeLaunchDescription,
    SetEnvironmentVariable,
    TimerAction,
)
from launch.conditions import IfCondition, UnlessCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description() -> LaunchDescription:
    headless = LaunchConfiguration("headless")
    use_rviz = LaunchConfiguration("use_rviz")
    use_sim_time = LaunchConfiguration("use_sim_time")
    x_pose = LaunchConfiguration("x_pose")
    y_pose = LaunchConfiguration("y_pose")

    world = LaunchConfiguration("world")
    map_yaml = LaunchConfiguration("map")
    nav2_params = LaunchConfiguration("params_file")
    rviz_config = LaunchConfiguration("rviz_config")

    ros_gz_launch = PathJoinSubstitution(
        [FindPackageShare("ros_gz_sim"), "launch", "gz_sim.launch.py"]
    )
    turtlebot_launch = PathJoinSubstitution([FindPackageShare("turtlebot3_gazebo"), "launch"])
    nav2_launch = PathJoinSubstitution(
        [FindPackageShare("nav2_bringup"), "launch", "bringup_launch.py"]
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument("headless", default_value="false"),
            DeclareLaunchArgument("use_rviz", default_value="true"),
            DeclareLaunchArgument("use_sim_time", default_value="true"),
            DeclareLaunchArgument("x_pose", default_value="-2.0"),
            DeclareLaunchArgument("y_pose", default_value="-0.5"),
            DeclareLaunchArgument(
                "world",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("turtlebot3_gazebo"), "worlds", "turtlebot3_world.world"]
                ),
            ),
            DeclareLaunchArgument(
                "map",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("turtlebot3_navigation2"), "map", "map.yaml"]
                ),
            ),
            DeclareLaunchArgument(
                "params_file",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("turtlebot3_navigation2"), "param", "burger.yaml"]
                ),
            ),
            DeclareLaunchArgument(
                "rviz_config",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("runtime_simulation"), "rviz", "runtime_nav2.rviz"]
                ),
            ),
            SetEnvironmentVariable("TURTLEBOT3_MODEL", "burger"),
            AppendEnvironmentVariable(
                "GZ_SIM_RESOURCE_PATH",
                PathJoinSubstitution([FindPackageShare("turtlebot3_gazebo"), "models"]),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(ros_gz_launch),
                launch_arguments={
                    "gz_args": ["-r -s -v2 ", world],
                    "on_exit_shutdown": "true",
                }.items(),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(ros_gz_launch),
                launch_arguments={"gz_args": "-g -v2", "on_exit_shutdown": "true"}.items(),
                condition=UnlessCondition(headless),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([turtlebot_launch, "robot_state_publisher.launch.py"])
                ),
                launch_arguments={"use_sim_time": use_sim_time}.items(),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([turtlebot_launch, "spawn_turtlebot3.launch.py"])
                ),
                launch_arguments={"x_pose": x_pose, "y_pose": y_pose}.items(),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(nav2_launch),
                launch_arguments={
                    "map": map_yaml,
                    "params_file": nav2_params,
                    "use_sim_time": use_sim_time,
                    "autostart": "true",
                }.items(),
            ),
            TimerAction(
                period=5.0,
                actions=[
                    Node(
                        package="runtime_simulation",
                        executable="initial_pose_publisher",
                        output="screen",
                        parameters=[
                            {
                                "use_sim_time": use_sim_time,
                                "x": ParameterValue(x_pose, value_type=float),
                                "y": ParameterValue(y_pose, value_type=float),
                                "yaw": 0.0,
                                "max_wait_s": 45.0,
                            }
                        ],
                    )
                ],
            ),
            Node(
                package="runtime_simulation",
                executable="scene_marker_publisher",
                output="screen",
                parameters=[{"use_sim_time": use_sim_time}],
            ),
            Node(
                package="task_executor",
                executable="execute_task_server",
                output="screen",
                parameters=[{"use_sim_time": use_sim_time}],
            ),
            Node(
                package="rviz2",
                executable="rviz2",
                name="rviz2",
                arguments=["-d", rviz_config],
                parameters=[{"use_sim_time": use_sim_time}],
                output="screen",
                condition=IfCondition(use_rviz),
            ),
        ]
    )
