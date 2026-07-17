import json
from pathlib import Path

import pytest

from agent_gateway.mission_plan import MissionPlanError
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan


SCHEMA_PATH = Path(__file__).parents[1] / "schema" / "mission_plan.schema.json"


def payload(*steps: dict) -> dict:
    return {"contract_version": 1, "steps": list(steps)}


def step(target: str, deadline_s: int = 30) -> dict:
    return {"action": "navigate", "target": target, "deadline_s": deadline_s}


def parse(value: dict):
    return parse_mission_plan(json.dumps(value), SCHEMA_PATH)


def test_accepts_two_step_plan_and_computes_budget() -> None:
    plan = parse(payload(step("dock", 90), step("workbench", 60)))

    assert plan.contract_version == 1
    assert tuple(item.target for item in plan.steps) == ("dock", "workbench")
    assert plan.budget_s == 150


def test_loads_valid_draft7_schema() -> None:
    schema = load_mission_plan_schema(SCHEMA_PATH)

    assert schema["properties"]["steps"]["maxItems"] == 3


@pytest.mark.parametrize(
    "invalid",
    [
        payload(),
        payload(step("dock"), step("home"), step("workbench"), step("dock")),
        payload(step("laboratory")),
        payload(step("dock", 91)),
        payload(step("dock", 61), step("home", 60), step("workbench", 60)),
        payload(step("dock"), step("dock")),
        {"contract_version": 1, "steps": [step("dock")], "extra": True},
        payload({**step("dock"), "task_id": "model-owned"}),
    ],
)
def test_rejects_invalid_mission_contract_or_semantics(invalid: dict) -> None:
    with pytest.raises(MissionPlanError):
        parse(invalid)


def test_rejects_duplicate_json_fields() -> None:
    raw = (
        '{"contract_version":1,"steps":['
        '{"action":"navigate","target":"dock","target":"home",'
        '"deadline_s":30}]}'
    )

    with pytest.raises(MissionPlanError, match="duplicate JSON field: target"):
        parse_mission_plan(raw, SCHEMA_PATH)


@pytest.mark.parametrize("raw", ["", "not-json", "[]"])
def test_rejects_non_object_json_text(raw: str) -> None:
    with pytest.raises(MissionPlanError):
        parse_mission_plan(raw, SCHEMA_PATH)
