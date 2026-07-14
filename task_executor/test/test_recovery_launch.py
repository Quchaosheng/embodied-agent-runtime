import launch
import launch_ros.actions
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import rclpy
import unittest
from action_msgs.msg import GoalStatus
from rclpy.action import ActionClient
from task_contract.action import ExecuteTask


def generate_test_description():
    retry_navigation = launch_ros.actions.Node(
        package="task_executor",
        executable="fake_navigate_to_pose_server",
        namespace="retry_success",
        parameters=[{"abort_first_n_goals": 1}],
        output="screen",
    )
    retry_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="retry_success",
        output="screen",
    )
    exhausted_navigation = launch_ros.actions.Node(
        package="task_executor",
        executable="fake_navigate_to_pose_server",
        namespace="recovery_exhausted",
        parameters=[{"abort_first_n_goals": 2}],
        output="screen",
    )
    exhausted_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="recovery_exhausted",
        output="screen",
    )
    deadline_navigation = launch_ros.actions.Node(
        package="task_executor",
        executable="fake_navigate_to_pose_server",
        namespace="retry_deadline",
        parameters=[{"abort_first_n_goals": 1, "feedback_delay_ms": 250}],
        output="screen",
    )
    deadline_executor = launch_ros.actions.Node(
        package="task_executor",
        executable="execute_task_server",
        namespace="retry_deadline",
        output="screen",
    )
    return launch.LaunchDescription(
        [
            retry_navigation,
            retry_executor,
            exhausted_navigation,
            exhausted_executor,
            deadline_navigation,
            deadline_executor,
            launch_testing.actions.ReadyToTest(),
        ]
    )


class TestBoundedRecovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        rclpy.shutdown()

    def setUp(self):
        self.node = rclpy.create_node(f"recovery_launch_test_{self._testMethodName}")

    def tearDown(self):
        self.node.destroy_node()

    def wait_for(self, future, timeout_sec=8.0):
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=timeout_sec)
        self.assertTrue(future.done())
        return future.result()

    def execute_task(self, action_name, task_id, deadline_s=5):
        feedback = []
        client = ActionClient(self.node, ExecuteTask, action_name)
        self.assertTrue(client.wait_for_server(timeout_sec=5.0))

        goal = ExecuteTask.Goal()
        goal.contract_version = 1
        goal.action = ExecuteTask.Goal.ACTION_NAVIGATE
        goal.task_id = task_id
        goal.target = "dock"
        goal.deadline_s = deadline_s
        goal_future = client.send_goal_async(
            goal,
            feedback_callback=lambda message: feedback.append(
                (
                    message.feedback.state,
                    message.feedback.attempt,
                    message.feedback.distance_remaining,
                )
            ),
        )
        goal_handle = self.wait_for(goal_future)
        self.assertTrue(goal_handle.accepted)
        response = self.wait_for(goal_handle.get_result_async())
        return response, feedback

    def test_first_failure_retries_and_succeeds(self):
        response, feedback = self.execute_task(
            "/retry_success/execute_task", "recovery-success"
        )

        self.assertEqual(response.status, GoalStatus.STATUS_SUCCEEDED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_SUCCEEDED)
        self.assertEqual(response.result.error_code, 0)
        self.assertEqual(response.result.attempts, 2)
        running_attempts = {
            attempt
            for state, attempt, _ in feedback
            if state == ExecuteTask.Feedback.STATE_RUNNING
        }
        self.assertEqual(running_attempts, {1, 2})
        recovering = [
            attempt
            for state, attempt, _ in feedback
            if state == ExecuteTask.Feedback.STATE_RECOVERING
        ]
        self.assertEqual(recovering, [1])

    def test_two_failures_end_in_safe_stop(self):
        response, feedback = self.execute_task(
            "/recovery_exhausted/execute_task", "recovery-exhausted"
        )

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_SAFE_STOP)
        self.assertEqual(response.result.error_code, 34)
        self.assertEqual(response.result.attempts, 2)
        recovering = [
            attempt
            for state, attempt, _ in feedback
            if state == ExecuteTask.Feedback.STATE_RECOVERING
        ]
        self.assertEqual(recovering, [1])

    def test_retry_does_not_reset_deadline(self):
        response, feedback = self.execute_task(
            "/retry_deadline/execute_task", "recovery-deadline", deadline_s=1
        )

        self.assertEqual(response.status, GoalStatus.STATUS_ABORTED)
        self.assertEqual(response.result.final_state, ExecuteTask.Result.STATE_FAILED)
        self.assertEqual(response.result.error_code, 32)
        self.assertEqual(response.result.attempts, 2)
        recovering = [
            attempt
            for state, attempt, _ in feedback
            if state == ExecuteTask.Feedback.STATE_RECOVERING
        ]
        self.assertEqual(recovering, [1])


@launch_testing.post_shutdown_test()
class TestProcessExit(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
