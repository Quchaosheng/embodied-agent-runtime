import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ament_index_python.packages import get_package_share_directory

from agent_gateway.provider import ModelProvider
from agent_gateway.provider import ProviderError
from agent_gateway.task_request import TaskRequestError
from agent_gateway.task_request import parse_task_request


class EvaluationConfigError(ValueError):
    pass


@dataclass(frozen=True)
class IntentCase:
    case_id: str
    text: str
    expected_target: str | None


@dataclass(frozen=True)
class IntentResult:
    case: IntentCase
    actual_target: str | None
    passed: bool
    detail: str


@dataclass(frozen=True)
class IntentReport:
    results: tuple[IntentResult, ...]

    @property
    def passed(self) -> int:
        return sum(result.passed for result in self.results)

    @property
    def total(self) -> int:
        return len(self.results)

    @property
    def accuracy(self) -> float:
        return self.passed / self.total if self.total else 0.0


def _default_cases_path() -> Path:
    share_directory = Path(get_package_share_directory("agent_gateway"))
    return share_directory / "evaluation" / "intent_cases.json"


def load_intent_cases(
    schema: dict[str, Any], cases_path: str | Path | None = None
) -> tuple[IntentCase, ...]:
    resolved_path = Path(cases_path) if cases_path else _default_cases_path()
    try:
        with resolved_path.open(encoding="utf-8") as cases_file:
            payload = json.load(cases_file)
    except (OSError, json.JSONDecodeError) as error:
        raise EvaluationConfigError(f"cannot load intent cases: {error}") from error

    if not isinstance(payload, dict) or set(payload) != {"version", "cases"}:
        raise EvaluationConfigError("evaluation root must contain only version and cases")
    if payload["version"] != 1 or not isinstance(payload["cases"], list):
        raise EvaluationConfigError("evaluation version must be 1 and cases must be a list")

    allowed_targets = set(schema["properties"]["target"]["enum"])
    cases: list[IntentCase] = []
    seen_ids: set[str] = set()
    for index, item in enumerate(payload["cases"]):
        if not isinstance(item, dict) or set(item) != {
            "id",
            "text",
            "expected_target",
        }:
            raise EvaluationConfigError(f"case {index} has unexpected fields")
        case_id = item["id"]
        text = item["text"]
        expected_target = item["expected_target"]
        if not isinstance(case_id, str) or not case_id.strip():
            raise EvaluationConfigError(f"case {index} id must be non-empty text")
        if case_id in seen_ids:
            raise EvaluationConfigError(f"duplicate case id: {case_id}")
        if not isinstance(text, str) or not text.strip():
            raise EvaluationConfigError(f"case {case_id} text must be non-empty")
        if expected_target is not None and expected_target not in allowed_targets:
            raise EvaluationConfigError(
                f"case {case_id} expected_target is outside the task contract"
            )
        seen_ids.add(case_id)
        cases.append(IntentCase(case_id, text, expected_target))

    if not cases:
        raise EvaluationConfigError("evaluation cases must not be empty")
    return tuple(cases)


def evaluate_intents(
    provider: ModelProvider,
    cases: tuple[IntentCase, ...],
    schema: dict[str, Any],
) -> IntentReport:
    results: list[IntentResult] = []
    for case in cases:
        try:
            raw_json = provider.generate_task(case.text, schema)
            request = parse_task_request(raw_json)
        except (ProviderError, TaskRequestError) as error:
            results.append(
                IntentResult(
                    case=case,
                    actual_target=None,
                    passed=case.expected_target is None,
                    detail=str(error),
                )
            )
            continue

        expected_rejection = case.expected_target is None
        results.append(
            IntentResult(
                case=case,
                actual_target=request.target,
                passed=not expected_rejection
                and request.target == case.expected_target,
                detail=(
                    "provider accepted a request that should be rejected"
                    if expected_rejection
                    else "target matched"
                    if request.target == case.expected_target
                    else "target mismatch"
                ),
            )
        )
    return IntentReport(tuple(results))


def format_intent_report(report: IntentReport) -> str:
    lines: list[str] = []
    for result in report.results:
        status = "PASS" if result.passed else "FAIL"
        expected = result.case.expected_target or "REJECT"
        actual = result.actual_target or "REJECT"
        lines.append(
            f"[{status}] {result.case.case_id}: expected={expected} actual={actual}"
        )
    lines.append(
        f"Intent evaluation: {report.passed}/{report.total} passed "
        f"({report.accuracy:.1%})"
    )
    return "\n".join(lines)
