import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from ament_index_python.packages import get_package_share_directory
from jsonschema import Draft7Validator


class MissionPlanError(ValueError):
    pass


class _DuplicateKeyError(ValueError):
    pass


@dataclass(frozen=True)
class MissionStep:
    action: str
    target: str
    deadline_s: int


@dataclass(frozen=True)
class MissionPlan:
    contract_version: int
    steps: tuple[MissionStep, ...]
    budget_s: int


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateKeyError(key)
        result[key] = value
    return result


def _default_schema_path() -> Path:
    share_directory = Path(get_package_share_directory("agent_gateway"))
    return share_directory / "schema" / "mission_plan.schema.json"


def load_mission_plan_schema(
    schema_path: str | Path | None = None,
) -> dict[str, Any]:
    resolved_path = Path(schema_path) if schema_path else _default_schema_path()
    with resolved_path.open(encoding="utf-8") as schema_file:
        schema = json.load(schema_file)
    Draft7Validator.check_schema(schema)
    return schema


@lru_cache(maxsize=8)
def _validator(schema_path: str) -> Draft7Validator:
    return Draft7Validator(load_mission_plan_schema(schema_path))


def parse_mission_plan(
    raw_json: str,
    schema_path: str | Path | None = None,
) -> MissionPlan:
    if not isinstance(raw_json, str) or not raw_json.strip():
        raise MissionPlanError("mission plan must be JSON text")
    try:
        payload = json.loads(raw_json, object_pairs_hook=_reject_duplicate_keys)
    except _DuplicateKeyError as error:
        raise MissionPlanError(f"duplicate JSON field: {error}") from error
    except json.JSONDecodeError as error:
        raise MissionPlanError(
            f"malformed JSON at line {error.lineno}, column {error.colno}"
        ) from error

    resolved_path = Path(schema_path) if schema_path else _default_schema_path()
    errors = sorted(
        _validator(str(resolved_path)).iter_errors(payload),
        key=lambda error: tuple(str(part) for part in error.absolute_path),
    )
    if errors:
        error = errors[0]
        location = ".".join(str(part) for part in error.absolute_path)
        detail = error.message if not location else f"{location}: {error.message}"
        raise MissionPlanError(detail)

    steps = tuple(MissionStep(**item) for item in payload["steps"])
    budget_s = sum(step.deadline_s for step in steps)
    if budget_s > 180:
        raise MissionPlanError("mission deadline budget exceeds 180 seconds")
    if any(left.target == right.target for left, right in zip(steps, steps[1:])):
        raise MissionPlanError("adjacent mission targets must differ")

    return MissionPlan(payload["contract_version"], steps, budget_s)
