"""
The process topology of the runtime, in one place.

Seven processes have to agree on a few values that used to be repeated in the
industrial E2E script and in the README by hand. Two of them are dangerous to
get wrong:

* ``database_path`` -- history and the gateway have defaulted to the relative
  ``runtime_history.sqlite3``, so starting them from different directories
  silently produced two databases. Here both read one absolute path.
* ``stage_event_log_path`` -- the executor, the bridge and the gateway append to
  the same file so a single capture spans the whole task path.

Everything else (timeouts, poll slices, retry counts) stays in each package's
own params file; this file only decides which processes run and with which
shared values.
"""

import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def _params(package, filename):
    return os.path.join(package, 'config', filename)


def _launch_setup(context):
    database_path = LaunchConfiguration('database_path').perform(context)
    stage_event_log_path = LaunchConfiguration('stage_event_log_path').perform(context)
    can_interface = LaunchConfiguration('can_interface').perform(context)
    gateway_port = LaunchConfiguration('gateway_port').perform(context)
    device_mode = LaunchConfiguration('device_mode').perform(context)
    use_virtual_device = LaunchConfiguration('use_virtual_device').perform(context) == 'true'
    respawn = LaunchConfiguration('respawn').perform(context) == 'true'

    # SQLite will not create a directory, and a missing one turns into
    # "unable to open database file" at node startup. Create both parents here
    # so the failure mode is about the database, not about the path.
    for path in (database_path, stage_event_log_path):
        parent = os.path.dirname(os.path.abspath(path))
        os.makedirs(parent, exist_ok=True)

    nodes = []

    if use_virtual_device:
        # Software ECU. Present so the stack is runnable without hardware; a
        # deployment with a real ECU sets use_virtual_device:=false.
        nodes.append(
            Node(
                package='virtual_can_device',
                executable='virtual_can_device_node',
                name='virtual_can_device',
                parameters=[
                    _params(
                        FindPackageShare('virtual_can_device').perform(context),
                        'device.yaml',
                    ),
                    {'interface_name': can_interface, 'mode': device_mode},
                ],
                output='screen',
                respawn=respawn,
                respawn_delay=2.0,
            )
        )

    nodes.extend([
        Node(
            package='device_bridge',
            executable='device_bridge_node',
            name='device_bridge',
            parameters=[
                _params(FindPackageShare('device_bridge').perform(context), 'device_bridge.yaml'),
                {'interface_name': can_interface, 'stage_event_log_path': stage_event_log_path},
            ],
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
        Node(
            package='task_executor',
            executable='task_executor_node',
            name='task_executor',
            parameters=[
                _params(FindPackageShare('task_executor').perform(context), 'targets.yaml'),
                {'stage_event_log_path': stage_event_log_path},
            ],
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
        Node(
            package='runtime_monitor',
            executable='runtime_monitor_node',
            name='runtime_monitor',
            parameters=[
                _params(
                    FindPackageShare('runtime_monitor').perform(context),
                    'runtime_monitor.yaml'),
            ],
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
        Node(
            package='runtime_history',
            executable='runtime_history_node',
            name='runtime_history',
            parameters=[{'database_path': database_path}],
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
        # No params file: the orchestrator resolves workflows.xml from its own
        # share directory, so there is nothing here that can disagree with it.
        Node(
            package='task_orchestrator',
            executable='task_orchestrator_node',
            name='task_orchestrator',
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
        Node(
            package='runtime_gateway',
            executable='runtime_gateway_node',
            name='runtime_gateway',
            parameters=[
                {
                    'port': gateway_port,
                    'database_path': database_path,
                    'stage_event_log_path': stage_event_log_path,
                },
            ],
            output='screen',
            respawn=respawn,
            respawn_delay=2.0,
        ),
    ])
    return nodes


def generate_launch_description():
    """Declare the arguments the operator may override, then build the stack."""
    return LaunchDescription([
        DeclareLaunchArgument(
            'database_path',
            default_value='/var/tmp/embodied-agent-runtime/runtime_history.sqlite3',
            description='SQLite history file. Shared by runtime_history and runtime_gateway.',
        ),
        DeclareLaunchArgument(
            'stage_event_log_path',
            default_value='/var/tmp/embodied-agent-runtime/stage_events.jsonl',
            description='RuntimeEvent v2 file the bridge, executor and gateway append to.',
        ),
        DeclareLaunchArgument(
            'can_interface',
            default_value='vcan0',
            description='SocketCAN interface for the bridge and the software ECU.',
        ),
        DeclareLaunchArgument(
            'gateway_port',
            default_value='50051',
            description='Loopback gRPC port the gateway listens on.',
        ),
        DeclareLaunchArgument(
            'device_mode',
            default_value='normal',
            description='Mode of the software ECU: normal, fault, delay_ack, drop_ack, '
                        'drop_stop_ack, duplicate or completion.',
        ),
        DeclareLaunchArgument(
            'use_virtual_device',
            default_value='true',
            description='Start the software ECU. Set false when a real ECU is on the bus.',
        ),
        DeclareLaunchArgument(
            'respawn',
            default_value='false',
            description='Restart a node that exits. Off by default: a crash is evidence, and '
                        'silently restarting the bridge hides it. Turn on only after the '
                        'fault-reset path exists.',
        ),
        OpaqueFunction(function=_launch_setup),
    ])
