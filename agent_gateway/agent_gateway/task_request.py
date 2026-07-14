import json
from dataclasses import dataclass
from enum import IntEnum
from functools import lru_cache
from pathlib import Path
from typing import Any

from ament_index_python.packages import get_package_share_directory
from jsonschema import Draft7Validator


class ErrorCode(IntEnum):
    INVALID_JSON = 10
    INVALID_CONTRACT = 11


class TaskRequestError(ValueError):
    def __init__(self, code: ErrorCode, detail: str) -> None:
        super().__init__(detail)
        self.code = code
        self.detail = detail


@dataclass(frozen=True)
class NormalizedTaskRequest:
    contract_version: int
    action: str
    task_id: str
    target: str
    deadline_s: int


class _DuplicateKeyError(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateKeyError(key)
        result[key] = value
    return result


def _default_schema_path() -> Path:
    share_directory = Path(get_package_share_directory("task_contract"))
    return share_directory / "schema" / "task_request.schema.json"


def load_task_request_schema(schema_path: str | Path | None = None) -> dict[str, Any]:
    resolved_schema_path = Path(schema_path) if schema_path else _default_schema_path()
    with resolved_schema_path.open(encoding="utf-8") as schema_file:
        schema = json.load(schema_file)
    Draft7Validator.check_schema(schema)
    return schema


@lru_cache(maxsize=8)
def _load_validator(schema_path: str) -> Draft7Validator:
    with Path(schema_path).open(encoding="utf-8") as schema_file:
        schema = json.load(schema_file)
    Draft7Validator.check_schema(schema)
    return Draft7Validator(schema)


def parse_task_request(
    raw_json: str, schema_path: str | Path | None = None
) -> NormalizedTaskRequest:
    if not isinstance(raw_json, str) or not raw_json.strip():
        raise TaskRequestError(ErrorCode.INVALID_JSON, "task request must be JSON text")

    try:
        payload = json.loads(raw_json, object_pairs_hook=_reject_duplicate_keys)
    except _DuplicateKeyError as error:
        raise TaskRequestError(
            ErrorCode.INVALID_JSON, f"duplicate JSON field: {error}"
        ) from error
    except json.JSONDecodeError as error:
        raise TaskRequestError(
            ErrorCode.INVALID_JSON,
            f"malformed JSON at line {error.lineno}, column {error.colno}",
        ) from error

    resolved_schema_path = Path(schema_path) if schema_path else _default_schema_path()
    validator = _load_validator(str(resolved_schema_path))
    errors = sorted(
        validator.iter_errors(payload),
        key=lambda error: tuple(str(part) for part in error.absolute_path),
    )
    if errors:
        error = errors[0]
        location = ".".join(str(part) for part in error.absolute_path)
        detail = error.message if not location else f"{location}: {error.message}"
        raise TaskRequestError(ErrorCode.INVALID_CONTRACT, detail)

    return NormalizedTaskRequest(
        contract_version=payload["contract_version"],
        action=payload["action"],
        task_id=payload.get("task_id", ""),
        target=payload["target"],
        deadline_s=payload["deadline_s"],
    )
