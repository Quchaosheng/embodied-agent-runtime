import json
import re
from typing import Any
from typing import Protocol

from agent_gateway.provider import OpenAICompatibleProvider
from agent_gateway.provider import PROVIDER_CHOICES
from agent_gateway.provider import ProviderError


class MissionModel(Protocol):
    def plan(self, user_text: str, schema: dict[str, Any]) -> str:
        pass

    def decide(self, checkpoint: dict[str, Any], choices: tuple[str, ...]) -> str:
        pass

    def summarize(self, trace: dict[str, Any]) -> str:
        pass


def _tool_arguments(response: dict[str, Any], expected_name: str) -> str:
    try:
        tool_calls = response["choices"][0]["message"]["tool_calls"]
    except (KeyError, IndexError, TypeError) as error:
        raise ProviderError("model did not call the approved mission tool") from error
    if not isinstance(tool_calls, list) or len(tool_calls) != 1:
        raise ProviderError("model must return exactly one mission tool call")
    tool_call = tool_calls[0]
    if not isinstance(tool_call, dict):
        raise ProviderError("mission tool call must be a JSON object")
    function = tool_call.get("function")
    if not isinstance(function, dict) or function.get("name") != expected_name:
        raise ProviderError("model called an unexpected mission tool")
    arguments = function.get("arguments")
    if not isinstance(arguments, str):
        raise ProviderError("mission tool arguments must be JSON text")
    return arguments


def _strict_object(raw_json: str) -> dict[str, Any]:
    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ProviderError(f"duplicate mission tool field: {key}")
            result[key] = value
        return result

    try:
        value = json.loads(raw_json, object_pairs_hook=reject_duplicates)
    except json.JSONDecodeError as error:
        raise ProviderError("mission tool arguments are invalid JSON") from error
    if not isinstance(value, dict):
        raise ProviderError("mission tool arguments must be a JSON object")
    return value


class FakeMissionModel:
    _target_pattern = re.compile(
        r"充电桩|充电|dock|工作台|workbench|回家|home",
        re.IGNORECASE,
    )

    def plan(self, user_text: str, schema: dict[str, Any]) -> str:
        del schema
        normalized = user_text.strip()
        if not normalized:
            raise ProviderError("mission text must not be empty")
        unsafe_markers = (
            "不要",
            "不去",
            "别去",
            "取消",
            "not ",
            "don't",
            "don’t",
            " or ",
            "或者",
            "忽略",
            "prompt",
            "坐标",
        )
        if any(word in normalized.lower() for word in unsafe_markers):
            raise ProviderError("fake mission model rejected unsafe or ambiguous intent")

        aliases = {
            "充电桩": "dock",
            "充电": "dock",
            "dock": "dock",
            "工作台": "workbench",
            "workbench": "workbench",
            "回家": "home",
            "home": "home",
        }
        targets = [
            aliases[match.group(0).lower()]
            for match in self._target_pattern.finditer(normalized)
        ]
        if not targets:
            raise ProviderError("fake mission model found no approved target")
        if len(targets) > 3:
            raise ProviderError("fake mission model found more than three steps")
        if any(left == right for left, right in zip(targets, targets[1:])):
            raise ProviderError("fake mission model found adjacent duplicate targets")

        return json.dumps(
            {
                "contract_version": 1,
                "steps": [
                    {"action": "navigate", "target": target, "deadline_s": 60}
                    for target in targets
                ],
            },
            ensure_ascii=False,
        )

    def decide(self, checkpoint: dict[str, Any], choices: tuple[str, ...]) -> str:
        del checkpoint
        return "continue" if "continue" in choices else "abort"

    def summarize(self, trace: dict[str, Any]) -> str:
        return f"任务结束：{trace.get('final_reason', 'unknown')}"


class OpenAICompatibleMissionModel(OpenAICompatibleProvider):
    def plan(self, user_text: str, schema: dict[str, Any]) -> str:
        if not user_text.strip():
            raise ProviderError("mission text must not be empty")
        tool_schema = {
            key: value for key, value in schema.items() if key not in {"$schema", "title"}
        }
        response = self._chat(
            [
                {
                    "role": "system",
                    "content": (
                        "Plan one bounded robot mission using only approved named targets. "
                        "For negated, ambiguous, or unsupported requests, do not call a tool. "
                        "Never invent coordinates, motion commands, retries, or tools."
                    ),
                },
                {"role": "user", "content": user_text},
            ],
            [
                {
                    "type": "function",
                    "function": {
                        "name": "submit_mission_plan",
                        "description": "Submit one bounded named-target mission",
                        "parameters": tool_schema,
                    },
                }
            ],
            "auto",
        )
        return _tool_arguments(response, "submit_mission_plan")

    def decide(self, checkpoint: dict[str, Any], choices: tuple[str, ...]) -> str:
        allowed = {"abort", "continue", "return_home"}
        if not choices or any(choice not in allowed for choice in choices):
            raise ProviderError("Runtime supplied invalid mission transitions")
        parameters = {
            "type": "object",
            "additionalProperties": False,
            "required": ["transition"],
            "properties": {
                "transition": {"type": "string", "enum": list(choices)}
            },
        }
        response = self._chat(
            [
                {
                    "role": "system",
                    "content": "Select exactly one Runtime-approved mission transition.",
                },
                {
                    "role": "user",
                    "content": json.dumps(checkpoint, ensure_ascii=False),
                },
            ],
            [
                {
                    "type": "function",
                    "function": {
                        "name": "select_mission_transition",
                        "description": "Select the next bounded mission transition",
                        "parameters": parameters,
                    },
                }
            ],
            "auto",
        )
        arguments = _strict_object(
            _tool_arguments(response, "select_mission_transition")
        )
        if set(arguments) != {"transition"} or arguments["transition"] not in choices:
            raise ProviderError("model selected a disallowed mission transition")
        return arguments["transition"]

    def summarize(self, trace: dict[str, Any]) -> str:
        response = self._chat(
            [
                {
                    "role": "system",
                    "content": "Summarize this completed robot mission in concise Chinese.",
                },
                {"role": "user", "content": json.dumps(trace, ensure_ascii=False)},
            ]
        )
        try:
            summary = response["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as error:
            raise ProviderError("model summary response is malformed") from error
        if not isinstance(summary, str) or not summary.strip() or len(summary) > 500:
            raise ProviderError("model summary must be 1..500 text characters")
        return summary.strip()


def create_mission_model(provider_name: str) -> MissionModel:
    if provider_name == "fake":
        return FakeMissionModel()
    if provider_name == "openai":
        return OpenAICompatibleMissionModel.from_openai_environment()
    if provider_name == "openai-compatible":
        return OpenAICompatibleMissionModel.from_environment()
    raise ProviderError(f"unsupported mission provider: {provider_name}")


MISSION_PROVIDER_CHOICES = PROVIDER_CHOICES
