from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription(
        [
            Node(
                package="device_bridge",
                executable="socketcan_heartbeat_node",
                output="screen",
                parameters=[
                    {
                        "interface": "vcan0",
                        "heartbeat_can_id": 0x321,
                        "heartbeat_timeout_ms": 500,
                    }
                ],
            )
        ]
    )
