from types import SimpleNamespace

import pytest
from action_msgs.msg import GoalStatus
from task_contract.action import ExecuteTask

from agent_gateway import ActionBridgeError
from agent_gateway import NormalizedTaskRequest
from agent_gateway import build_execute_task_goal
from agent_gateway.action_bridge import outcome_from_response
from agent_gateway.action_bridge import progress_from_feedback


def request(task_id: str = "") -> NormalizedTaskRequest:
    return NormalizedTaskRequest(
        contract_version=1,
        action="navigate",
        task_id=task_id,
        target="dock",
        deadline_s=30,
    )


def test_builds_execute_task_goal_and_generates_task_id() -> None:
    goal = build_execute_task_goal(request(), task_id_factory=lambda: "generated-id")

    assert goal.contract_version == 1
    assert goal.action == ExecuteTask.Goal.ACTION_NAVIGATE
    assert goal.task_id == "generated-id"
    assert goal.target == "dock"
    assert goal.deadline_s == 30


def test_preserves_model_independent_task_id() -> None:
    goal = build_execute_task_goal(request("caller-task"))

    assert goal.task_id == "caller-task"


def test_rejects_unsupported_normalized_action() -> None:
    unsupported = NormalizedTaskRequest(1, "dance", "task", "dock", 30)

    with pytest.raises(ActionBridgeError):
        build_execute_task_goal(unsupported)


def test_converts_feedback_to_stable_progress() -> None:
    feedback = ExecuteTask.Feedback()
    feedback.state = ExecuteTask.Feedback.STATE_RECOVERING
    feedback.attempt = 1
    feedback.distance_remaining = 2.5
    feedback.detail = "retrying"

    progress = progress_from_feedback(feedback)

    assert progress.state_name == "RECOVERING"
    assert progress.attempt == 1
    assert progress.distance_remaining == pytest.approx(2.5)
    assert progress.detail == "retrying"


def test_converts_action_result_to_stable_outcome() -> None:
    result = ExecuteTask.Result()
    result.final_state = ExecuteTask.Result.STATE_SAFE_STOP
    result.error_code = 34
    result.attempts = 2
    result.detail = "recovery exhausted"
    response = SimpleNamespace(status=GoalStatus.STATUS_ABORTED, result=result)

    outcome = outcome_from_response(response, "task-34")

    assert outcome.task_id == "task-34"
    assert outcome.goal_status_name == "ABORTED"
    assert outcome.final_state_name == "SAFE_STOP"
    assert outcome.error_code == 34
    assert outcome.attempts == 2
