import json
import os
from collections.abc import Callable
from typing import Any
from typing import Protocol
from urllib.error import HTTPError
from urllib.error import URLError
from urllib.parse import urlparse
from urllib.request import Request
from urllib.request import urlopen


class ProviderError(ValueError):
    pass


class ModelProvider(Protocol):
    def generate_task(self, user_text: str, schema: dict[str, Any]) -> str:
        pass


JsonTransport = Callable[
    [str, dict[str, str], dict[str, Any], float], dict[str, Any]
]


def _post_json(
    url: str,
    headers: dict[str, str],
    payload: dict[str, Any],
    timeout_s: float,
) -> dict[str, Any]:
    request = Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    try:
        with urlopen(request, timeout=timeout_s) as response:
            raw_body = response.read(1_000_001)
    except (HTTPError, URLError, TimeoutError) as error:
        raise ProviderError("model provider request failed") from error

    if len(raw_body) > 1_000_000:
        raise ProviderError("model provider response exceeds 1 MB")
    try:
        decoded = json.loads(raw_body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProviderError("model provider returned invalid JSON") from error
    if not isinstance(decoded, dict):
        raise ProviderError("model provider response must be a JSON object")
    return decoded


class OpenAICompatibleProvider:
    tool_name = "submit_robot_task"

    def __init__(
        self,
        base_url: str,
        model: str,
        api_key: str = "",
        timeout_s: float = 30.0,
        transport: JsonTransport = _post_json,
    ) -> None:
        parsed_url = urlparse(base_url)
        if parsed_url.scheme not in {"http", "https"} or not parsed_url.netloc:
            raise ProviderError("model base URL must use http or https")
        if parsed_url.username or parsed_url.password:
            raise ProviderError("model base URL must not contain credentials")
        if parsed_url.query or parsed_url.fragment:
            raise ProviderError("model base URL must not contain query or fragment")
        if parsed_url.scheme == "http" and parsed_url.hostname not in {
            "localhost",
            "127.0.0.1",
            "::1",
        }:
            raise ProviderError("remote model base URL must use https")
        if not model.strip():
            raise ProviderError("model name must not be empty")
        if timeout_s <= 0:
            raise ProviderError("model timeout must be greater than zero")

        normalized_url = base_url.rstrip("/")
        self._endpoint = (
            normalized_url
            if normalized_url.endswith("/chat/completions")
            else normalized_url + "/chat/completions"
        )
        self._model = model
        self._api_key = api_key
        self._timeout_s = timeout_s
        self._transport = transport

    @classmethod
    def from_environment(cls) -> "OpenAICompatibleProvider":
        base_url = os.environ.get("EMBODIED_AI_BASE_URL", "")
        model = os.environ.get("EMBODIED_AI_MODEL", "")
        api_key = os.environ.get("EMBODIED_AI_API_KEY", "")
        timeout_text = os.environ.get("EMBODIED_AI_TIMEOUT_S", "30")
        try:
            timeout_s = float(timeout_text)
        except ValueError as error:
            raise ProviderError("EMBODIED_AI_TIMEOUT_S must be numeric") from error
        if not base_url:
            raise ProviderError("EMBODIED_AI_BASE_URL is required")
        if not model:
            raise ProviderError("EMBODIED_AI_MODEL is required")
        return cls(base_url, model, api_key, timeout_s)

    @classmethod
    def from_openai_environment(cls) -> "OpenAICompatibleProvider":
        base_url = os.environ.get("OPENAI_BASE_URL", "https://api.openai.com/v1")
        model = os.environ.get("OPENAI_MODEL", "")
        api_key = os.environ.get("OPENAI_API_KEY", "")
        timeout_text = os.environ.get("EMBODIED_AI_TIMEOUT_S", "30")
        try:
            timeout_s = float(timeout_text)
        except ValueError as error:
            raise ProviderError("EMBODIED_AI_TIMEOUT_S must be numeric") from error
        if not model:
            raise ProviderError("OPENAI_MODEL is required")
        if not api_key:
            raise ProviderError("OPENAI_API_KEY is required")
        return cls(base_url, model, api_key, timeout_s)

    def generate_task(self, user_text: str, schema: dict[str, Any]) -> str:
        if not user_text.strip():
            raise ProviderError("user text must not be empty")

        tool_schema = {
            key: value
            for key, value in schema.items()
            if key not in {"$schema", "title"}
        }
        payload = {
            "model": self._model,
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "Interpret the user intent. Call submit_robot_task exactly once "
                        "only when the user clearly requests one approved named target. "
                        "Otherwise do not call any tool. "
                        "Never invent coordinates, velocities, paths, retries, or tools."
                    ),
                },
                {"role": "user", "content": user_text},
            ],
            "tools": [
                {
                    "type": "function",
                    "function": {
                        "name": self.tool_name,
                        "description": "Submit one approved named robot task",
                        "parameters": tool_schema,
                    },
                }
            ],
            "tool_choice": "auto",
            "temperature": 0,
        }
        headers = {"Content-Type": "application/json"}
        if self._api_key:
            headers["Authorization"] = f"Bearer {self._api_key}"

        response = self._transport(self._endpoint, headers, payload, self._timeout_s)
        try:
            tool_calls = response["choices"][0]["message"]["tool_calls"]
        except (KeyError, IndexError, TypeError) as error:
            raise ProviderError("model did not produce an approved task") from error
        if not isinstance(tool_calls, list) or not tool_calls:
            raise ProviderError("model did not produce an approved task")
        if len(tool_calls) != 1:
            raise ProviderError("model must return exactly one tool call")

        tool_call = tool_calls[0]
        if not isinstance(tool_call, dict):
            raise ProviderError("model tool call must be a JSON object")
        function = tool_call.get("function")
        if not isinstance(function, dict) or function.get("name") != self.tool_name:
            raise ProviderError("model called an unexpected tool")
        arguments = function.get("arguments")
        if not isinstance(arguments, str):
            raise ProviderError("tool arguments must be JSON text")
        return arguments


class FakeModelProvider:
    def generate_task(self, user_text: str, schema: dict[str, Any]) -> str:
        del schema
        normalized = user_text.strip().lower()
        if not normalized:
            raise ProviderError("user text must not be empty")

        target_keywords = {
            "dock": ("充电", "dock"),
            "workbench": ("工作台", "workbench"),
            "home": ("回家", "home"),
        }
        matched_targets = {
            target
            for target, keywords in target_keywords.items()
            if any(keyword in normalized for keyword in keywords)
        }
        negations = ("不要", "别去", "别回", "取消", "not ")
        if len(matched_targets) != 1 or any(
            negation in normalized for negation in negations
        ):
            raise ProviderError("fake provider cannot map the request to an approved target")
        target = matched_targets.pop()

        return json.dumps(
            {
                "contract_version": 1,
                "action": "navigate",
                "target": target,
                "deadline_s": 30,
            },
            ensure_ascii=False,
        )


PROVIDER_CHOICES = ("fake", "openai", "openai-compatible")


def create_model_provider(provider_name: str) -> ModelProvider:
    if provider_name == "fake":
        return FakeModelProvider()
    if provider_name == "openai":
        return OpenAICompatibleProvider.from_openai_environment()
    if provider_name == "openai-compatible":
        return OpenAICompatibleProvider.from_environment()
    raise ProviderError(f"unsupported model provider: {provider_name}")
