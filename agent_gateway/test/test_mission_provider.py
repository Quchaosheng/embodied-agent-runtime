import json
from pathlib import Path

import pytest

from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan
from agent_gateway.mission_provider import FakeMissionModel
from agent_gateway.mission_provider import OpenAICompatibleMissionModel
from agent_gateway.provider import ProviderError


SCHEMA_PATH = Path(__file__).parents[1] / "schema" / "mission_plan.schema.json"


def tool_response(name: str, arguments: str) -> dict:
    return {
        "choices": [
            {
                "message": {
                    "tool_calls": [
                        {"function": {"name": name, "arguments": arguments}}
                    ]
                }
            }
        ]
    }


def test_fake_model_plans_ordered_named_targets() -> None:
    model = FakeMissionModel()

    raw = model.plan("先去充电桩，再去工作台", load_mission_plan_schema(SCHEMA_PATH))

    assert tuple(step.target for step in parse_mission_plan(raw, SCHEMA_PATH).steps) == (
        "dock",
        "workbench",
    )


@pytest.mark.parametrize("text", ["不要去充电桩", "帮我拿水", "dock dock"])
def test_fake_model_rejects_unsafe_or_invalid_missions(text: str) -> None:
    with pytest.raises(ProviderError):
        FakeMissionModel().plan(text, load_mission_plan_schema(SCHEMA_PATH))


def test_fake_model_selects_only_offered_transition() -> None:
    model = FakeMissionModel()

    assert model.decide({"last_step_succeeded": True}, ("abort", "continue")) == (
        "continue"
    )
    assert model.decide({"last_step_succeeded": False}, ("abort",)) == "abort"


def test_fake_summary_is_bounded() -> None:
    summary = FakeMissionModel().summarize({"final_reason": "completed"})

    assert summary
    assert len(summary) <= 500


def test_compatible_model_exposes_closed_mission_plan_tool() -> None:
    captured = {}

    def transport(url, headers, payload, timeout_s):
        captured.update(url=url, headers=headers, payload=payload, timeout_s=timeout_s)
        return tool_response(
            "submit_mission_plan",
            json.dumps(
                {
                    "contract_version": 1,
                    "steps": [
                        {"action": "navigate", "target": "dock", "deadline_s": 30}
                    ],
                }
            ),
        )

    model = OpenAICompatibleMissionModel(
        "https://provider.example/v1", "test-model", "secret", transport=transport
    )
    raw = model.plan("去充电桩", load_mission_plan_schema(SCHEMA_PATH))

    assert parse_mission_plan(raw, SCHEMA_PATH).steps[0].target == "dock"
    function = captured["payload"]["tools"][0]["function"]
    assert function["name"] == "submit_mission_plan"
    assert function["parameters"]["additionalProperties"] is False
    assert captured["payload"]["tool_choice"] == "auto"


def test_compatible_model_limits_decision_tool_to_offered_choices() -> None:
    captured = {}

    def transport(url, headers, payload, timeout_s):
        captured["payload"] = payload
        return tool_response("select_mission_transition", '{"transition":"abort"}')

    model = OpenAICompatibleMissionModel(
        "https://provider.example/v1", "test-model", transport=transport
    )
    decision = model.decide(
        {"last_step_succeeded": False}, ("abort", "return_home")
    )

    assert decision == "abort"
    parameters = captured["payload"]["tools"][0]["function"]["parameters"]
    assert parameters["properties"]["transition"]["enum"] == [
        "abort",
        "return_home",
    ]


def test_compatible_model_rejects_multiple_tool_calls() -> None:
    response = tool_response("submit_mission_plan", "{}")
    response["choices"][0]["message"]["tool_calls"].append(
        {"function": {"name": "submit_mission_plan", "arguments": "{}"}}
    )
    model = OpenAICompatibleMissionModel(
        "https://provider.example/v1",
        "test-model",
        transport=lambda *_: response,
    )

    with pytest.raises(ProviderError, match="exactly one"):
        model.plan("去充电桩", load_mission_plan_schema(SCHEMA_PATH))


@pytest.mark.parametrize(
    "response",
    [
        {"choices": [{"message": {"content": ""}}]},
        {"choices": [{"message": {"content": "x" * 501}}]},
        {"choices": [{"message": {"content": 42}}]},
    ],
)
def test_compatible_model_rejects_invalid_summary(response: dict) -> None:
    model = OpenAICompatibleMissionModel(
        "https://provider.example/v1",
        "test-model",
        transport=lambda *_: response,
    )

    with pytest.raises(ProviderError, match="summary"):
        model.summarize({"final_reason": "completed"})
