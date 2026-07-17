import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ament_index_python.packages import get_package_share_directory

from agent_gateway.mission_plan import MissionPlanError
from agent_gateway.mission_plan import parse_mission_plan
from agent_gateway.mission_provider import MissionModel
from agent_gateway.provider import ProviderError


class EvaluationConfigError(ValueError):
    pass


@dataclass(frozen=True)
class MissionCase:
    case_id: str
    text: str
    expected_targets: tuple[str, ...] | None


@dataclass(frozen=True)
class MissionEvaluationResult:
    case: MissionCase
    actual_targets: tuple[str, ...] | None
    passed: bool
    detail: str


@dataclass(frozen=True)
class MissionEvaluationReport:
    results: tuple[MissionEvaluationResult, ...]
    elapsed_s: float
    request_count: int

    @property
    def passed(self) -> int:
        return sum(result.passed for result in self.results)

    @property
    def total(self) -> int:
        return len(self.results)

    @property
    def unsafe_acceptances(self) -> int:
        return sum(
            result.case.expected_targets is None
            and result.actual_targets is not None
            for result in self.results
        )


def _default_cases_path() -> Path:
    share_directory = Path(get_package_share_directory("agent_gateway"))
    return share_directory / "evaluation" / "mission_cases.json"


def load_mission_cases(
    schema: dict[str, Any],
    cases_path: str | Path | None = None,
) -> tuple[MissionCase, ...]:
    resolved_path = Path(cases_path) if cases_path else _default_cases_path()
    try:
        with resolved_path.open(encoding="utf-8") as cases_file:
            payload = json.load(cases_file)
    except (OSError, json.JSONDecodeError) as error:
        raise EvaluationConfigError(f"cannot load mission cases: {error}") from error

    if not isinstance(payload, dict) or set(payload) != {"version", "cases"}:
        raise EvaluationConfigError("evaluation root must contain version and cases")
    if payload["version"] != 1 or not isinstance(payload["cases"], list):
        raise EvaluationConfigError("evaluation version must be 1 and cases must be a list")

    allowed_targets = set(
        schema["properties"]["steps"]["items"]["properties"]["target"]["enum"]
    )
    cases: list[MissionCase] = []
    seen_ids: set[str] = set()
    for index, item in enumerate(payload["cases"]):
        if not isinstance(item, dict) or set(item) != {
            "id",
            "text",
            "expected_targets",
        }:
            raise EvaluationConfigError(f"case {index} has unexpected fields")
        case_id = item["id"]
        text = item["text"]
        expected = item["expected_targets"]
        if not isinstance(case_id, str) or not case_id.strip():
            raise EvaluationConfigError(f"case {index} id must be non-empty text")
        if case_id in seen_ids:
            raise EvaluationConfigError(f"duplicate case id: {case_id}")
        if not isinstance(text, str) or not text.strip():
            raise EvaluationConfigError(f"case {case_id} text must be non-empty")
        if expected is not None:
            if (
                not isinstance(expected, list)
                or not 1 <= len(expected) <= 3
                or any(target not in allowed_targets for target in expected)
            ):
                raise EvaluationConfigError(
                    f"case {case_id} expected_targets are outside the mission contract"
                )
            expected_targets = tuple(expected)
        else:
            expected_targets = None
        seen_ids.add(case_id)
        cases.append(MissionCase(case_id, text, expected_targets))

    if not cases:
        raise EvaluationConfigError("evaluation cases must not be empty")
    return tuple(cases)


def evaluate_missions(
    model: MissionModel,
    cases: tuple[MissionCase, ...],
    schema: dict[str, Any],
    schema_path: str | Path | None = None,
    clock=time.monotonic,
) -> MissionEvaluationReport:
    started = clock()
    results: list[MissionEvaluationResult] = []
    for case in cases:
        try:
            raw_plan = model.plan(case.text, schema)
            plan = parse_mission_plan(raw_plan, schema_path)
        except (ProviderError, MissionPlanError) as error:
            results.append(
                MissionEvaluationResult(
                    case,
                    None,
                    case.expected_targets is None,
                    str(error),
                )
            )
            continue

        actual_targets = tuple(step.target for step in plan.steps)
        expected_rejection = case.expected_targets is None
        results.append(
            MissionEvaluationResult(
                case,
                actual_targets,
                not expected_rejection and actual_targets == case.expected_targets,
                (
                    "model accepted a mission that should be rejected"
                    if expected_rejection
                    else "targets matched"
                    if actual_targets == case.expected_targets
                    else "target sequence mismatch"
                ),
            )
        )
    return MissionEvaluationReport(tuple(results), clock() - started, len(cases))


def format_mission_report(report: MissionEvaluationReport) -> str:
    lines = []
    for result in report.results:
        status = "PASS" if result.passed else "FAIL"
        expected = result.case.expected_targets or ("REJECT",)
        actual = result.actual_targets or ("REJECT",)
        lines.append(
            f"[{status}] {result.case.case_id}: "
            f"expected={','.join(expected)} actual={','.join(actual)}"
        )
    lines.append(
        f"Mission evaluation: {report.passed}/{report.total} passed; "
        f"unsafe_acceptances={report.unsafe_acceptances}; "
        f"requests={report.request_count}; elapsed_s={report.elapsed_s:.3f}"
    )
    return "\n".join(lines)
