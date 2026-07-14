import json
from pathlib import Path

import pytest

from agent_gateway import load_task_request_schema
from agent_gateway.intent_evaluation import EvaluationConfigError
from agent_gateway.intent_evaluation import evaluate_intents
from agent_gateway.intent_evaluation import format_intent_report
from agent_gateway.intent_evaluation import load_intent_cases
from agent_gateway.provider import FakeModelProvider


CASES_PATH = Path(__file__).parents[1] / "evaluation" / "intent_cases.json"


def test_fixed_intent_set_passes_with_fake_provider() -> None:
    schema = load_task_request_schema()
    cases = load_intent_cases(schema, CASES_PATH)

    report = evaluate_intents(FakeModelProvider(), cases, schema)

    assert report.total == 20
    assert report.passed == 20
    assert report.accuracy == 1.0
    assert "Intent evaluation: 20/20 passed (100.0%)" in format_intent_report(report)


def test_intent_set_rejects_duplicate_case_ids(tmp_path: Path) -> None:
    cases_path = tmp_path / "duplicate.json"
    cases_path.write_text(
        json.dumps(
            {
                "version": 1,
                "cases": [
                    {"id": "same", "text": "回家", "expected_target": "home"},
                    {"id": "same", "text": "去工作台", "expected_target": "workbench"},
                ],
            },
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )

    with pytest.raises(EvaluationConfigError, match="duplicate case id"):
        load_intent_cases(load_task_request_schema(), cases_path)


def test_intent_set_rejects_target_outside_contract(tmp_path: Path) -> None:
    cases_path = tmp_path / "unknown-target.json"
    cases_path.write_text(
        json.dumps(
            {
                "version": 1,
                "cases": [
                    {
                        "id": "unknown",
                        "text": "去实验室",
                        "expected_target": "laboratory",
                    }
                ],
            },
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )

    with pytest.raises(EvaluationConfigError, match="outside the task contract"):
        load_intent_cases(load_task_request_schema(), cases_path)
