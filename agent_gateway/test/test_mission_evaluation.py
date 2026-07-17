import json
from pathlib import Path

import pytest

from agent_gateway.mission_evaluation import EvaluationConfigError
from agent_gateway.mission_evaluation import evaluate_missions
from agent_gateway.mission_evaluation import format_mission_report
from agent_gateway.mission_evaluation import load_mission_cases
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_provider import FakeMissionModel


ROOT = Path(__file__).parents[1]
SCHEMA_PATH = ROOT / "schema" / "mission_plan.schema.json"
CASES_PATH = ROOT / "evaluation" / "mission_cases.json"


def test_fixed_mission_set_passes_with_fake_model() -> None:
    schema = load_mission_plan_schema(SCHEMA_PATH)
    cases = load_mission_cases(schema, CASES_PATH)

    report = evaluate_missions(
        FakeMissionModel(), cases, schema, schema_path=SCHEMA_PATH
    )

    assert report.total == 12
    assert report.passed == 12
    assert report.unsafe_acceptances == 0
    assert report.request_count == 12
    assert "12/12" in format_mission_report(report)


def test_rejects_duplicate_case_ids(tmp_path: Path) -> None:
    payload = json.loads(CASES_PATH.read_text(encoding="utf-8"))
    payload["cases"][1]["id"] = payload["cases"][0]["id"]
    invalid = tmp_path / "cases.json"
    invalid.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(EvaluationConfigError, match="duplicate case id"):
        load_mission_cases(load_mission_plan_schema(SCHEMA_PATH), invalid)


def test_rejects_expected_target_outside_contract(tmp_path: Path) -> None:
    payload = json.loads(CASES_PATH.read_text(encoding="utf-8"))
    payload["cases"][0]["expected_targets"] = ["laboratory"]
    invalid = tmp_path / "cases.json"
    invalid.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(EvaluationConfigError, match="outside the mission contract"):
        load_mission_cases(load_mission_plan_schema(SCHEMA_PATH), invalid)


def test_rejects_unexpected_case_fields(tmp_path: Path) -> None:
    payload = json.loads(CASES_PATH.read_text(encoding="utf-8"))
    payload["cases"][0]["deadline_s"] = 90
    invalid = tmp_path / "cases.json"
    invalid.write_text(json.dumps(payload), encoding="utf-8")

    with pytest.raises(EvaluationConfigError, match="unexpected fields"):
        load_mission_cases(load_mission_plan_schema(SCHEMA_PATH), invalid)
