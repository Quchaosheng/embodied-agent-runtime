import json

import pytest

from agent_gateway import ErrorCode
from agent_gateway import TaskRequestError
from agent_gateway import parse_task_request


def valid_payload() -> dict:
    return {
        "contract_version": 1,
        "action": "navigate",
        "target": "dock",
        "deadline_s": 90,
    }


def test_accepts_valid_task_and_normalizes_missing_task_id() -> None:
    request = parse_task_request(json.dumps(valid_payload()))

    assert request.contract_version == 1
    assert request.action == "navigate"
    assert request.task_id == ""
    assert request.target == "dock"
    assert request.deadline_s == 90


def test_preserves_valid_task_id() -> None:
    payload = valid_payload()
    payload["task_id"] = "project-demo"

    request = parse_task_request(json.dumps(payload))

    assert request.task_id == "project-demo"


def test_rejects_malformed_json() -> None:
    with pytest.raises(TaskRequestError) as caught:
        parse_task_request('{"action": "navigate"')

    assert caught.value.code == ErrorCode.INVALID_JSON


def test_rejects_duplicate_fields() -> None:
    with pytest.raises(TaskRequestError) as caught:
        parse_task_request(
            '{"contract_version":1,"action":"navigate","target":"dock",'
            '"target":"home","deadline_s":90}'
        )

    assert caught.value.code == ErrorCode.INVALID_JSON


@pytest.mark.parametrize(
    "field,value",
    [
        ("target", "laboratory"),
        ("deadline_s", 91),
        ("deadline_s", "90"),
    ],
)
def test_rejects_invalid_contract_values(field: str, value: object) -> None:
    payload = valid_payload()
    payload[field] = value

    with pytest.raises(TaskRequestError) as caught:
        parse_task_request(json.dumps(payload))

    assert caught.value.code == ErrorCode.INVALID_CONTRACT


def test_rejects_extra_fields() -> None:
    payload = valid_payload()
    payload["x"] = 1

    with pytest.raises(TaskRequestError) as caught:
        parse_task_request(json.dumps(payload))

    assert caught.value.code == ErrorCode.INVALID_CONTRACT


def test_rejects_missing_required_field() -> None:
    payload = valid_payload()
    del payload["target"]

    with pytest.raises(TaskRequestError) as caught:
        parse_task_request(json.dumps(payload))

    assert caught.value.code == ErrorCode.INVALID_CONTRACT
