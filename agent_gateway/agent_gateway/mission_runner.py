import math
import time
from collections.abc import Callable
from dataclasses import asdict
from dataclasses import dataclass
from typing import Any
from uuid import uuid4

from agent_gateway.action_bridge import ActionBridgeError
from agent_gateway.action_bridge import TaskOutcome
from agent_gateway.mission_plan import MissionPlan
from agent_gateway.mission_provider import MissionModel
from agent_gateway.provider import ProviderError
from agent_gateway.task_request import NormalizedTaskRequest


@dataclass(frozen=True)
class MissionCheckpoint:
    completed_targets: tuple[str, ...]
    pending_targets: tuple[str, ...]
    last_step_succeeded: bool
    last_outcome: dict[str, Any] | None
    last_error: str
    elapsed_s: float


@dataclass(frozen=True)
class MissionStepRecord:
    request: NormalizedTaskRequest
    outcome: TaskOutcome | None
    error: str


@dataclass(frozen=True)
class MissionTrace:
    mission_id: str
    records: tuple[MissionStepRecord, ...]
    decisions: tuple[str, ...]
    final_reason: str
    elapsed_s: float


@dataclass(frozen=True)
class MissionResult:
    trace: MissionTrace
    summary: str


StepExecutor = Callable[[NormalizedTaskRequest, Callable | None], TaskOutcome]


def _succeeded(outcome: TaskOutcome) -> bool:
    return (
        outcome.error_code == 0
        and outcome.goal_status_name == "SUCCEEDED"
        and outcome.final_state_name == "SUCCEEDED"
    )


def _local_summary(trace: MissionTrace) -> str:
    return f"Mission ended: {trace.final_reason}"


class MissionRunner:
    def __init__(
        self,
        model: MissionModel,
        step_executor: StepExecutor,
        clock: Callable[[], float] = time.monotonic,
        mission_id_factory: Callable[[], str] = lambda: uuid4().hex,
    ) -> None:
        self._model = model
        self._step_executor = step_executor
        self._clock = clock
        self._mission_id_factory = mission_id_factory

    def run(
        self,
        plan: MissionPlan,
        feedback_callback: Callable | None = None,
    ) -> MissionResult:
        started = self._clock()
        mission_id = self._mission_id_factory()
        records: list[MissionStepRecord] = []
        decisions: list[str] = []
        final_reason = "deadline_expired"

        for index, step in enumerate(plan.steps):
            remaining_s = plan.budget_s - (self._clock() - started)
            deadline_s = min(step.deadline_s, math.floor(remaining_s))
            if deadline_s < 1:
                final_reason = "deadline_expired"
                break

            request = NormalizedTaskRequest(
                contract_version=plan.contract_version,
                action=step.action,
                task_id=f"{mission_id}-{index + 1}",
                target=step.target,
                deadline_s=deadline_s,
            )
            try:
                step_outcome = self._step_executor(request, feedback_callback)
            except ActionBridgeError as error:
                records.append(MissionStepRecord(request, None, str(error)))
                final_reason = "action_bridge_error"
                break

            records.append(MissionStepRecord(request, step_outcome, ""))
            step_succeeded = _succeeded(step_outcome)
            pending = plan.steps[index + 1 :]
            if step_succeeded and not pending:
                final_reason = "completed"
                break

            elapsed_s = self._clock() - started
            remaining_s = plan.budget_s - elapsed_s
            if remaining_s < 1:
                final_reason = "deadline_expired"
                break

            choices = ["abort"]
            if step_succeeded and pending:
                choices.append("continue")
            if step.target != "home" and math.floor(remaining_s) >= 1:
                choices.append("return_home")

            checkpoint = MissionCheckpoint(
                completed_targets=tuple(
                    record.request.target
                    for record in records
                    if record.outcome is not None and _succeeded(record.outcome)
                ),
                pending_targets=tuple(item.target for item in pending),
                last_step_succeeded=step_succeeded,
                last_outcome=asdict(step_outcome),
                last_error="",
                elapsed_s=elapsed_s,
            )
            try:
                decision = self._model.decide(asdict(checkpoint), tuple(choices))
            except ProviderError:
                final_reason = "checkpoint_provider_failed"
                break
            decisions.append(decision)
            if decision not in choices:
                final_reason = "disallowed_transition"
                break
            if decision == "abort":
                final_reason = "aborted_by_model"
                break
            if decision == "return_home":
                remaining_s = plan.budget_s - (self._clock() - started)
                home_deadline_s = min(90, math.floor(remaining_s))
                if home_deadline_s < 1:
                    final_reason = "deadline_expired"
                    break
                home_request = NormalizedTaskRequest(
                    contract_version=plan.contract_version,
                    action="navigate",
                    task_id=f"{mission_id}-home",
                    target="home",
                    deadline_s=home_deadline_s,
                )
                try:
                    home_outcome = self._step_executor(
                        home_request, feedback_callback
                    )
                except ActionBridgeError as error:
                    records.append(MissionStepRecord(home_request, None, str(error)))
                    final_reason = "return_home_failed"
                else:
                    records.append(MissionStepRecord(home_request, home_outcome, ""))
                    final_reason = (
                        "returned_home"
                        if _succeeded(home_outcome)
                        else "return_home_failed"
                    )
                break

        trace = MissionTrace(
            mission_id=mission_id,
            records=tuple(records),
            decisions=tuple(decisions),
            final_reason=final_reason,
            elapsed_s=self._clock() - started,
        )
        try:
            summary = self._model.summarize(asdict(trace))
        except ProviderError:
            summary = _local_summary(trace)
        return MissionResult(trace, summary)
