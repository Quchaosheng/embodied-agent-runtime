from dataclasses import dataclass
from typing import Callable
from uuid import uuid4

import rclpy
from action_msgs.msg import GoalStatus
from rclpy.action import ActionClient
from rclpy.node import Node
from task_contract.action import ExecuteTask

from agent_gateway.task_request import NormalizedTaskRequest


class ActionBridgeError(RuntimeError):
    pass


@dataclass(frozen=True)
class TaskProgress:
    state: int
    state_name: str
    attempt: int
    distance_remaining: float
    detail: str


@dataclass(frozen=True)
class TaskOutcome:
    task_id: str
    goal_status: int
    goal_status_name: str
    final_state: int
    final_state_name: str
    error_code: int
    detail: str
    attempts: int


_FEEDBACK_STATE_NAMES = {
    ExecuteTask.Feedback.STATE_VALIDATING: "VALIDATING",
    ExecuteTask.Feedback.STATE_DISPATCHING: "DISPATCHING",
    ExecuteTask.Feedback.STATE_RUNNING: "RUNNING",
    ExecuteTask.Feedback.STATE_RECOVERING: "RECOVERING",
    ExecuteTask.Feedback.STATE_CANCELLING: "CANCELLING",
}

_RESULT_STATE_NAMES = {
    ExecuteTask.Result.STATE_SUCCEEDED: "SUCCEEDED",
    ExecuteTask.Result.STATE_CANCELLED: "CANCELLED",
    ExecuteTask.Result.STATE_SAFE_STOP: "SAFE_STOP",
    ExecuteTask.Result.STATE_FAILED: "FAILED",
}

_GOAL_STATUS_NAMES = {
    GoalStatus.STATUS_UNKNOWN: "UNKNOWN",
    GoalStatus.STATUS_ACCEPTED: "ACCEPTED",
    GoalStatus.STATUS_EXECUTING: "EXECUTING",
    GoalStatus.STATUS_CANCELING: "CANCELING",
    GoalStatus.STATUS_SUCCEEDED: "SUCCEEDED",
    GoalStatus.STATUS_CANCELED: "CANCELED",
    GoalStatus.STATUS_ABORTED: "ABORTED",
}


def build_execute_task_goal(
    request: NormalizedTaskRequest,
    task_id_factory: Callable[[], str] = lambda: uuid4().hex,
) -> ExecuteTask.Goal:
    if request.action != "navigate":
        raise ActionBridgeError(f"unsupported normalized action: {request.action}")

    goal = ExecuteTask.Goal()
    goal.contract_version = request.contract_version
    goal.action = ExecuteTask.Goal.ACTION_NAVIGATE
    goal.task_id = request.task_id or task_id_factory()
    goal.target = request.target
    goal.deadline_s = request.deadline_s
    return goal


def progress_from_feedback(feedback: ExecuteTask.Feedback) -> TaskProgress:
    return TaskProgress(
        state=feedback.state,
        state_name=_FEEDBACK_STATE_NAMES.get(feedback.state, "UNKNOWN"),
        attempt=feedback.attempt,
        distance_remaining=feedback.distance_remaining,
        detail=feedback.detail,
    )


def outcome_from_response(response: object, task_id: str) -> TaskOutcome:
    result = response.result
    return TaskOutcome(
        task_id=task_id,
        goal_status=response.status,
        goal_status_name=_GOAL_STATUS_NAMES.get(response.status, "UNKNOWN"),
        final_state=result.final_state,
        final_state_name=_RESULT_STATE_NAMES.get(result.final_state, "UNKNOWN"),
        error_code=result.error_code,
        detail=result.detail,
        attempts=result.attempts,
    )


class ExecuteTaskClient(Node):
    def __init__(self, action_name: str = "/execute_task") -> None:
        super().__init__("agent_gateway_execute_task_client")
        self._client = ActionClient(self, ExecuteTask, action_name)

    def execute(
        self,
        request: NormalizedTaskRequest,
        feedback_callback: Callable[[TaskProgress], None] | None = None,
        server_timeout_s: float = 5.0,
    ) -> TaskOutcome:
        if not self._client.wait_for_server(timeout_sec=server_timeout_s):
            raise ActionBridgeError("ExecuteTask Action Server is unavailable")

        goal = build_execute_task_goal(request)

        def receive_feedback(message: object) -> None:
            if feedback_callback is not None:
                feedback_callback(progress_from_feedback(message.feedback))

        goal_future = self._client.send_goal_async(goal, feedback_callback=receive_feedback)
        rclpy.spin_until_future_complete(self, goal_future, timeout_sec=server_timeout_s)
        if not goal_future.done():
            raise ActionBridgeError("ExecuteTask Goal response timed out")

        goal_handle = goal_future.result()
        if not goal_handle.accepted:
            raise ActionBridgeError("ExecuteTask Goal was rejected")

        result_future = goal_handle.get_result_async()
        result_timeout_s = float(request.deadline_s) + server_timeout_s
        rclpy.spin_until_future_complete(self, result_future, timeout_sec=result_timeout_s)
        if not result_future.done():
            cancel_future = goal_handle.cancel_goal_async()
            rclpy.spin_until_future_complete(self, cancel_future, timeout_sec=server_timeout_s)
            raise ActionBridgeError("ExecuteTask result timed out locally")

        return outcome_from_response(result_future.result(), goal.task_id)
