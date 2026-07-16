import time
import unittest

from action_msgs.msg import GoalStatus
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
import launch
import launch_ros.actions
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import rclpy
from rclpy.action import ActionClient
from std_msgs.msg import Bool
from task_contract.action import ExecuteTask


def generate_test_description():
    static_transform = launch_ros.actions.Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        arguments=["--frame-id", "map", "--child-frame-id", "base_link"],
        output="screen",
    )
    fake_navigation = launch_ros.actions.Node(
        package="task_executor",
        executable="fake_navigate_to_pose_server",
        output="screen",
    )
    ready_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="ready",
        remappings=[("navigate_to_pose", "/navigate_to_pose")],
        output="screen",
    )
    unlocalized_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="unlocalized",
        parameters=[{"base_frame": "missing_base"}],
        remappings=[("navigate_to_pose", "/navigate_to_pose")],
        output="screen",
    )
    navigation_unready_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="navigation_unready",
        parameters=[{"localization_check_enabled": False}],
        output="screen",
    )
    device_unready_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="device_required",
        parameters=[
            {
                "localization_check_enabled": False,
                "require_device_ready": True,
                "device_ready_timeout_ms": 300,
            }
        ],
        remappings=[("navigate_to_pose", "/navigate_to_pose")],
        output="screen",
    )
    return launch.LaunchDescription(
        [
            static_transform,
            fake_navigation,
            ready_executor,
            unlocalized_executor,
            navigation_unready_executor,
            device_unready_executor,
            launch_testing.actions.ReadyToTest(),
        ]
    )


class TestRuntimeReadiness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        rclpy.shutdown()

    def setUp(self):
        self.node = rclpy.create_node(f"readiness_launch_test_{self._testMethodName}")
        self.statuses = {}
        self.subscription = self.node.create_subscription(
            DiagnosticArray, "/diagnostics", self.on_diagnostics, 10
        )

    def tearDown(self):
        self.node.destroy_node()

    def on_diagnostics(self, message):
        for status in message.status:
            self.statuses[status.name] = status

    def wait_for(self, future, timeout_sec=5.0):
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=timeout_sec)
        self.assertTrue(future.done())
        return future.result()

    def wait_for_status(self, name, expected_values, timeout_sec=6.0):
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.1)
            status = self.statuses.get(name)
            if status is None:
                continue
            values = {item.key: item.value for item in status.values}
            if all(values.get(key) == value for key, value in expected_values.items()):
                return status
        self.fail(f"diagnostic status {name} did not reach {expected_values}")

    def execute_task(self, action_name, task_id):
        client = ActionClient(self.node, ExecuteTask, action_name)
        self.assertTrue(client.wait_for_server(timeout_sec=5.0))
        goal = ExecuteTask.Goal()
        goal.contract_version = 1
        goal.action = ExecuteTask.Goal.ACTION_NAVIGATE
        goal.task_id = task_id
        goal.target = "dock"
        goal.deadline_s = 5
        goal_handle = self.wait_for(client.send_goal_async(goal))
        self.assertTrue(goal_handle.accepted)
        return self.wait_for(goal_handle.get_result_async())

    def test_ready_transform_and_navigation_allow_task(self):
        status = self.wait_for_status(
            "/ready/execute_task_server/readiness",
            {"localization_ready": "true", "navigation_ready": "true"},
        )
        self.assertEqual(status.level, DiagnosticStatus.OK)

        response = self.execute_task("/ready/execute_task", "readiness-success")

        self.assertEqual(response.status, GoalStatus.STATUS_SUCCEEDED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_SUCCEEDED)

    def test_missing_localization_transform_rejects_task(self):
        status = self.wait_for_status(
            "/unlocalized/execute_task_server/readiness",
            {"localization_ready": "false", "navigation_ready": "true"},
        )
        self.assertEqual(status.level, DiagnosticStatus.ERROR)

        response = self.execute_task("/unlocalized/execute_task", "readiness-no-localization")

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.error_code, 16)
        self.assertEqual(response.result.attempts, 0)

    def test_missing_navigation_server_rejects_task(self):
        status = self.wait_for_status(
            "/navigation_unready/execute_task_server/readiness",
            {"localization_ready": "true", "navigation_ready": "false"},
        )
        self.assertEqual(status.level, DiagnosticStatus.ERROR)

        response = self.execute_task(
            "/navigation_unready/execute_task", "readiness-no-navigation"
        )

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.error_code, 17)
        self.assertEqual(response.result.attempts, 0)

    def test_device_heartbeat_gates_task_and_expires(self):
        status_name = "/device_required/execute_task_server/readiness"
        self.wait_for_status(
            status_name,
            {"device_ready": "false", "device_ready_required": "true"},
        )
        unavailable = self.execute_task(
            "/device_required/execute_task", "readiness-no-device"
        )
        self.assertEqual(unavailable.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(unavailable.result.error_code, 18)

        publisher = self.node.create_publisher(Bool, "/device_ready", 10)
        message = Bool()
        message.data = True
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            publisher.publish(message)
            rclpy.spin_once(self.node, timeout_sec=0.05)
        self.wait_for_status(status_name, {"device_ready": "true"})

        ready = self.execute_task("/device_required/execute_task", "readiness-device-ready")
        self.assertEqual(ready.status, GoalStatus.STATUS_SUCCEEDED)

        self.node.destroy_publisher(publisher)
        self.wait_for_status(status_name, {"device_ready": "false"}, timeout_sec=3.0)
        stale = self.execute_task("/device_required/execute_task", "readiness-device-stale")
        self.assertEqual(stale.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(stale.result.error_code, 18)


@launch_testing.post_shutdown_test()
class TestProcessExit(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
