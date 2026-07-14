import json

import pytest

from agent_gateway import load_task_request_schema
from agent_gateway import parse_task_request
from agent_gateway.provider import FakeModelProvider
from agent_gateway.provider import OpenAICompatibleProvider
from agent_gateway.provider import ProviderError
from agent_gateway.provider import create_model_provider


@pytest.mark.parametrize(
    "text,expected_target",
    [
        ("电量低了，回充电桩", "dock"),
        ("请去工作台", "workbench"),
        ("现在回家", "home"),
    ],
)
def test_fake_provider_maps_approved_intent(text: str, expected_target: str) -> None:
    provider = FakeModelProvider()

    raw_json = provider.generate_task(text, load_task_request_schema())
    request = parse_task_request(raw_json)

    assert request.target == expected_target
    assert request.action == "navigate"
    assert request.deadline_s == 30


def test_fake_provider_rejects_unknown_intent() -> None:
    provider = FakeModelProvider()

    with pytest.raises(ProviderError):
        provider.generate_task("帮我拿一杯水", load_task_request_schema())


def test_exposed_schema_remains_closed() -> None:
    schema = load_task_request_schema()

    assert schema["additionalProperties"] is False
    assert json.dumps(schema)


def compatible_response(arguments: str) -> dict:
    return {
        "choices": [
            {
                "message": {
                    "tool_calls": [
                        {
                            "function": {
                                "name": "submit_robot_task",
                                "arguments": arguments,
                            }
                        }
                    ]
                }
            }
        ]
    }


def test_compatible_provider_exposes_one_closed_task_tool() -> None:
    captured: dict = {}

    def transport(url: str, headers: dict, payload: dict, timeout_s: float) -> dict:
        captured.update(
            url=url,
            headers=headers,
            payload=payload,
            timeout_s=timeout_s,
        )
        return compatible_response(
            json.dumps(
                {
                    "contract_version": 1,
                    "action": "navigate",
                    "target": "workbench",
                    "deadline_s": 45,
                }
            )
        )

    provider = OpenAICompatibleProvider(
        "https://provider.example/v1",
        "test-model",
        api_key="secret-value",
        timeout_s=12,
        transport=transport,
    )
    raw_json = provider.generate_task("去工作台", load_task_request_schema())
    request = parse_task_request(raw_json)

    assert request.target == "workbench"
    assert captured["url"] == "https://provider.example/v1/chat/completions"
    assert captured["headers"]["Authorization"] == "Bearer secret-value"
    assert captured["timeout_s"] == 12
    function = captured["payload"]["tools"][0]["function"]
    assert function["name"] == "submit_robot_task"
    assert function["parameters"]["additionalProperties"] is False
    assert "$schema" not in function["parameters"]
    assert captured["payload"]["tool_choice"] == "auto"


def test_compatible_provider_rejects_missing_tool_call() -> None:
    provider = OpenAICompatibleProvider(
        "http://localhost:11434/v1",
        "local-model",
        transport=lambda _url, _headers, _payload, _timeout: {"choices": []},
    )

    with pytest.raises(ProviderError):
        provider.generate_task("回家", load_task_request_schema())


def test_compatible_provider_rejects_multiple_tool_calls() -> None:
    response = compatible_response("{}")
    response["choices"][0]["message"]["tool_calls"].append(
        {"function": {"name": "submit_robot_task", "arguments": "{}"}}
    )
    provider = OpenAICompatibleProvider(
        "http://localhost:11434/v1",
        "local-model",
        transport=lambda _url, _headers, _payload, _timeout: response,
    )

    with pytest.raises(ProviderError):
        provider.generate_task("回家", load_task_request_schema())


def test_compatible_provider_rejects_malformed_tool_call() -> None:
    provider = OpenAICompatibleProvider(
        "http://localhost:11434/v1",
        "local-model",
        transport=lambda _url, _headers, _payload, _timeout: {
            "choices": [{"message": {"tool_calls": ["invalid"]}}]
        },
    )

    with pytest.raises(ProviderError, match="must be a JSON object"):
        provider.generate_task("回家", load_task_request_schema())


def test_compatible_provider_rejects_invalid_base_url() -> None:
    with pytest.raises(ProviderError):
        OpenAICompatibleProvider("file:///tmp/model", "test-model")


def test_compatible_provider_rejects_remote_plain_http() -> None:
    with pytest.raises(ProviderError, match="must use https"):
        OpenAICompatibleProvider("http://relay.example/v1", "test-model")


def test_compatible_provider_accepts_full_chat_completions_url() -> None:
    captured: dict = {}

    def transport(url: str, _headers: dict, _payload: dict, _timeout: float) -> dict:
        captured["url"] = url
        return compatible_response(
            '{"contract_version":1,"action":"navigate",'
            '"target":"home","deadline_s":30}'
        )

    provider = OpenAICompatibleProvider(
        "https://relay.example/v1/chat/completions",
        "relay-model",
        transport=transport,
    )

    provider.generate_task("回家", load_task_request_schema())

    assert captured["url"] == "https://relay.example/v1/chat/completions"


def test_compatible_provider_reads_environment(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("EMBODIED_AI_BASE_URL", "http://localhost:11434/v1")
    monkeypatch.setenv("EMBODIED_AI_MODEL", "local-model")
    monkeypatch.setenv("EMBODIED_AI_TIMEOUT_S", "15")

    provider = OpenAICompatibleProvider.from_environment()

    assert provider is not None


def test_official_openai_profile_uses_standard_environment(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("OPENAI_BASE_URL", raising=False)
    monkeypatch.setenv("OPENAI_MODEL", "account-model")
    monkeypatch.setenv("OPENAI_API_KEY", "local-secret")

    provider = create_model_provider("openai")

    assert isinstance(provider, OpenAICompatibleProvider)
    assert provider._endpoint == "https://api.openai.com/v1/chat/completions"
    assert provider._model == "account-model"


def test_official_openai_profile_requires_api_key(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("OPENAI_MODEL", "account-model")
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)

    with pytest.raises(ProviderError, match="OPENAI_API_KEY is required"):
        create_model_provider("openai")
