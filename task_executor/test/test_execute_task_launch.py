import launch
import launch_ros.actions
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import rclpy
import time
import unittest
from action_msgs.msg import GoalStatus
from rclpy.action import ActionClient
from rclpy.qos import DurabilityPolicy
from rclpy.qos import QoSProfile
from rclpy.qos import ReliabilityPolicy
from task_contract.action import ExecuteTask
from task_contract.msg import TaskEvent


def task_event_qos():
    return QoSProfile(
        depth=50,
        reliability=ReliabilityPolicy.RELIABLE,
        durability=DurabilityPolicy.TRANSIENT_LOCAL,
    )


def generate_test_description():
    fake_navigation = launch_ros.actions.Node(
        package="task_executor",
        executable="fake_navigate_to_pose_server",
        output="screen",
        parameters=[{"feedback_delay_ms": 400}],
    )
    task_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        output="screen",
        parameters=[{"localization_check_enabled": False}],
    )
    return launch.LaunchDescription(
        [
            fake_navigation,
            task_executor,
            launch_testing.actions.ReadyToTest(),
        ]
    )


class TestExecuteTaskLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        rclpy.shutdown()

    def setUp(self):
        self.node = rclpy.create_node(f"execute_task_launch_test_{self._testMethodName}")
        self.client = ActionClient(self.node, ExecuteTask, "/execute_task")
        self.events = []
        self.event_subscription = self.node.create_subscription(
            TaskEvent,
            "/task_events",
            self.events.append,
            task_event_qos(),
        )
        self.assertTrue(self.client.wait_for_server(timeout_sec=5.0))

    def tearDown(self):
        self.node.destroy_node()

    def wait_for(self, future, timeout_sec=5.0):
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=timeout_sec)
        self.assertTrue(future.done())
        return future.result()

    def send_goal(self, target, task_id, feedback, deadline_s=3):
        goal = ExecuteTask.Goal()
        goal.contract_version = 1
        goal.action = ExecuteTask.Goal.ACTION_NAVIGATE
        goal.task_id = task_id
        goal.target = target
        goal.deadline_s = deadline_s
        future = self.client.send_goal_async(
            goal,
            feedback_callback=lambda message: feedback.append(
                message.feedback.distance_remaining
            ),
        )
        goal_handle = self.wait_for(future)
        self.assertTrue(goal_handle.accepted)
        return goal_handle

    def get_result(self, goal_handle):
        return self.wait_for(goal_handle.get_result_async())

    def wait_for_feedback(self, feedback, timeout_sec=5.0):
        deadline = time.monotonic() + timeout_sec
        while not feedback and time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.05)
        self.assertTrue(feedback)

    def wait_for_event_state(self, events, task_id, state, timeout_sec=5.0):
        deadline = time.monotonic() + timeout_sec
        while (
            not any(event.task_id == task_id and event.state == state for event in events)
            and time.monotonic() < deadline
        ):
            rclpy.spin_once(self.node, timeout_sec=0.05)
        self.assertTrue(
            any(event.task_id == task_id and event.state == state for event in events)
        )

    def events_for(self, events, task_id):
        return [event for event in events if event.task_id == task_id]

    def test_success_forwards_feedback_and_result(self):
        feedback = []
        goal_handle = self.send_goal("dock", "launch-success", feedback)
        response = self.get_result(goal_handle)

        self.assertEqual(response.status, GoalStatus.STATUS_SUCCEEDED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_SUCCEEDED)
        self.assertEqual(response.result.error_code, 0)
        self.assertEqual(response.result.attempts, 1)
        self.assertEqual(feedback, [3.0, 2.0, 1.0])
        self.wait_for_event_state(self.events, "launch-success", TaskEvent.STATE_SUCCEEDED)
        self.assertEqual(
            [event.state for event in self.events_for(self.events, "launch-success")],
            [
                TaskEvent.STATE_VALIDATING,
                TaskEvent.STATE_DISPATCHING,
                TaskEvent.STATE_RUNNING,
                TaskEvent.STATE_SUCCEEDED,
            ],
        )

    def test_unknown_target_aborts_before_navigation(self):
        feedback = []
        goal_handle = self.send_goal("laboratory", "launch-unknown", feedback)
        response = self.get_result(goal_handle)

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_FAILED)
        self.assertEqual(response.result.error_code, 13)
        self.assertEqual(response.result.attempts, 0)
        self.assertEqual(feedback, [])
        self.wait_for_event_state(self.events, "launch-unknown", TaskEvent.STATE_FAILED)
        events = self.events_for(self.events, "launch-unknown")
        self.assertEqual(
            [event.state for event in events],
            [TaskEvent.STATE_VALIDATING, TaskEvent.STATE_FAILED],
        )
        self.assertEqual(events[-1].error_code, 13)
        self.assertEqual(events[-1].attempt, 0)

    def test_cancel_propagates_to_navigation(self):
        feedback = []
        goal_handle = self.send_goal("dock", "launch-cancel", feedback)
        self.wait_for_feedback(feedback)
        cancel_response = self.wait_for(goal_handle.cancel_goal_async())
        self.assertEqual(len(cancel_response.goals_canceling), 1)

        response = self.get_result(goal_handle)
        self.assertEqual(response.status, GoalStatus.STATUS_CANCELED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_CANCELLED)
        self.assertEqual(response.result.error_code, 0)
        self.assertEqual(response.result.attempts, 1)
        self.wait_for_event_state(self.events, "launch-cancel", TaskEvent.STATE_CANCELLED)
        self.assertEqual(
            [event.state for event in self.events_for(self.events, "launch-cancel")],
            [
                TaskEvent.STATE_VALIDATING,
                TaskEvent.STATE_DISPATCHING,
                TaskEvent.STATE_RUNNING,
                TaskEvent.STATE_CANCELLING,
                TaskEvent.STATE_CANCELLED,
            ],
        )

    def test_deadline_cancels_navigation(self):
        feedback = []
        goal_handle = self.send_goal("dock", "launch-timeout", feedback, deadline_s=1)
        response = self.get_result(goal_handle)

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_FAILED)
        self.assertEqual(response.result.error_code, 32)
        self.assertEqual(response.result.attempts, 1)
        self.wait_for_event_state(self.events, "launch-timeout", TaskEvent.STATE_FAILED)
        events = self.events_for(self.events, "launch-timeout")
        self.assertEqual(
            [event.state for event in events],
            [
                TaskEvent.STATE_VALIDATING,
                TaskEvent.STATE_DISPATCHING,
                TaskEvent.STATE_RUNNING,
                TaskEvent.STATE_CANCELLING,
                TaskEvent.STATE_FAILED,
            ],
        )
        self.assertEqual(events[-1].error_code, 32)

    def test_late_subscriber_receives_retained_task_history(self):
        feedback = []
        task_id = "launch-retained-history"
        goal_handle = self.send_goal("dock", task_id, feedback)
        response = self.get_result(goal_handle)
        self.assertEqual(response.status, GoalStatus.STATUS_SUCCEEDED)

        late_events = []
        late_subscription = self.node.create_subscription(
            TaskEvent,
            "/task_events",
            late_events.append,
            task_event_qos(),
        )
        self.wait_for_event_state(late_events, task_id, TaskEvent.STATE_SUCCEEDED)

        retained = self.events_for(late_events, task_id)
        self.assertEqual(
            [event.state for event in retained],
            [
                TaskEvent.STATE_VALIDATING,
                TaskEvent.STATE_DISPATCHING,
                TaskEvent.STATE_RUNNING,
                TaskEvent.STATE_SUCCEEDED,
            ],
        )
        self.assertGreater(retained[-1].stamp.sec, 0)
        self.node.destroy_subscription(late_subscription)

    def test_second_goal_is_rejected_while_task_is_active(self):
        first_feedback = []
        first_goal = self.send_goal("dock", "launch-busy-owner", first_feedback)
        self.wait_for_feedback(first_feedback)

        second_goal = self.send_goal("home", "launch-busy-rejected", [])
        second_response = self.get_result(second_goal)

        self.assertEqual(second_response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(second_response.result.final_state, ExecuteTask.Result.STATE_FAILED)
        self.assertEqual(second_response.result.error_code, 15)
        self.assertEqual(second_response.result.attempts, 0)

        cancel_response = self.wait_for(first_goal.cancel_goal_async())
        self.assertEqual(len(cancel_response.goals_canceling), 1)
        first_response = self.get_result(first_goal)
        self.assertEqual(first_response.status, GoalStatus.STATUS_CANCELED)

        third_goal = self.send_goal("home", "launch-after-busy", [])
        third_response = self.get_result(third_goal)
        self.assertEqual(third_response.status, GoalStatus.STATUS_SUCCEEDED)
        self.assertEqual(third_response.result.error_code, 0)


@launch_testing.post_shutdown_test()
class TestProcessExit(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
