from collections.abc import Callable

from action_msgs.msg import GoalStatus

from agent_gateway.action_bridge import ActionBridgeError
from agent_gateway.action_bridge import TaskOutcome
from agent_gateway.mission_plan import MissionPlan
from agent_gateway.mission_plan import MissionStep
from agent_gateway.mission_runner import MissionRunner
from agent_gateway.provider import ProviderError


def plan(*targets: str, deadline_s: int = 60) -> MissionPlan:
    steps = tuple(MissionStep("navigate", target, deadline_s) for target in targets)
    return MissionPlan(1, steps, sum(step.deadline_s for step in steps))


def outcome(task_id: str, succeeded: bool = True) -> TaskOutcome:
    return TaskOutcome(
        task_id=task_id,
        goal_status=(
            GoalStatus.STATUS_SUCCEEDED if succeeded else GoalStatus.STATUS_ABORTED
        ),
        goal_status_name="SUCCEEDED" if succeeded else "ABORTED",
        final_state=5 if succeeded else 9,
        final_state_name="SUCCEEDED" if succeeded else "FAILED",
        error_code=0 if succeeded else 30,
        detail="navigation succeeded" if succeeded else "navigation failed",
        attempts=1,
    )


class RecordingExecutor:
    def __init__(self, results: list[bool | Exception]) -> None:
        self.results = list(results)
        self.requests = []

    def __call__(self, request, feedback_callback):
        del feedback_callback
        self.requests.append(request)
        result = self.results.pop(0)
        if isinstance(result, Exception):
            raise result
        return outcome(request.task_id, result)


class RecordingMissionModel:
    def __init__(
        self,
        decisions: list[str] | None = None,
        decision_error: Exception | None = None,
        summary_error: Exception | None = None,
    ) -> None:
        self.decisions = list(decisions or [])
        self.decision_error = decision_error
        self.summary_error = summary_error
        self.decision_calls = []
        self.summary_calls = []

    def plan(self, user_text, schema):
        raise AssertionError("runner must not plan")

    def decide(self, checkpoint, choices):
        self.decision_calls.append((checkpoint, choices))
        if self.decision_error:
            raise self.decision_error
        return self.decisions.pop(0)

    def summarize(self, trace):
        self.summary_calls.append(trace)
        if self.summary_error:
            raise self.summary_error
        return f"summary: {trace['final_reason']}"


class FakeClock:
    def __init__(self, *values: float) -> None:
        self.values = list(values) or [0.0]
        self.last = self.values[-1]

    def __call__(self) -> float:
        if self.values:
            self.last = self.values.pop(0)
        return self.last


def runner(
    model: RecordingMissionModel,
    executor: RecordingExecutor,
    clock: Callable[[], float] = lambda: 0.0,
) -> MissionRunner:
    return MissionRunner(
        model,
        executor,
        clock=clock,
        mission_id_factory=lambda: "mission-id",
    )


def test_two_successful_steps_run_in_order_and_skip_final_checkpoint() -> None:
    model = RecordingMissionModel(["continue"])
    executor = RecordingExecutor([True, True])

    result = runner(model, executor).run(plan("dock", "workbench"))

    assert [request.target for request in executor.requests] == ["dock", "workbench"]
    assert [request.task_id for request in executor.requests] == [
        "mission-id-1",
        "mission-id-2",
    ]
    assert len(model.decision_calls) == 1
    assert result.trace.final_reason == "completed"
    assert result.summary == "summary: completed"


def test_checkpoint_provider_failure_aborts_without_second_goal() -> None:
    model = RecordingMissionModel(decision_error=ProviderError("offline"))
    executor = RecordingExecutor([True])

    result = runner(model, executor).run(plan("dock", "workbench"))

    assert [request.target for request in executor.requests] == ["dock"]
    assert result.trace.final_reason == "checkpoint_provider_failed"


def test_model_abort_prevents_pending_step() -> None:
    model = RecordingMissionModel(["abort"])
    executor = RecordingExecutor([True])

    result = runner(model, executor).run(plan("dock", "workbench"))

    assert [request.target for request in executor.requests] == ["dock"]
    assert result.trace.decisions == ("abort",)
    assert result.trace.final_reason == "aborted_by_model"


def test_failed_step_can_return_home_once() -> None:
    model = RecordingMissionModel(["return_home"])
    executor = RecordingExecutor([False, True])

    result = runner(model, executor).run(plan("dock", "workbench"))

    assert [request.target for request in executor.requests] == ["dock", "home"]
    assert executor.requests[1].deadline_s == 90
    assert result.trace.final_reason == "returned_home"


def test_disallowed_transition_fails_closed() -> None:
    model = RecordingMissionModel(["dance"])
    executor = RecordingExecutor([True])

    result = runner(model, executor).run(plan("dock", "workbench"))

    assert len(executor.requests) == 1
    assert result.trace.final_reason == "disallowed_transition"


def test_deadline_expiry_prevents_second_goal_and_checkpoint_call() -> None:
    model = RecordingMissionModel(["continue"])
    executor = RecordingExecutor([True])
    clock = FakeClock(0.0, 0.0, 121.0, 121.0)

    result = runner(model, executor, clock).run(plan("dock", "workbench"))

    assert len(executor.requests) == 1
    assert not model.decision_calls
    assert result.trace.final_reason == "deadline_expired"


def test_action_bridge_error_aborts_without_model_decision() -> None:
    model = RecordingMissionModel(["return_home"])
    executor = RecordingExecutor([ActionBridgeError("server unavailable")])

    result = runner(model, executor).run(plan("dock"))

    assert not model.decision_calls
    assert result.trace.records[0].outcome is None
    assert result.trace.final_reason == "action_bridge_error"


def test_summary_provider_failure_uses_local_summary() -> None:
    model = RecordingMissionModel(summary_error=ProviderError("offline"))
    executor = RecordingExecutor([True])

    result = runner(model, executor).run(plan("dock"))

    assert result.trace.final_reason == "completed"
    assert "completed" in result.summary
