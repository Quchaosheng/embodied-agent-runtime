from pathlib import Path

import pytest
from action_msgs.msg import GoalStatus

from agent_gateway.action_bridge import TaskOutcome
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan
from agent_gateway.mission_provider import FakeMissionModel
from agent_gateway.provider import ProviderError


SCHEMA_PATH = Path(__file__).parents[1] / "schema" / "mission_plan.schema.json"


def configure_local_schema(monkeypatch, module) -> None:
    monkeypatch.setattr(
        module,
        "load_mission_plan_schema",
        lambda: load_mission_plan_schema(SCHEMA_PATH),
    )
    monkeypatch.setattr(
        module,
        "parse_mission_plan",
        lambda raw: parse_mission_plan(raw, SCHEMA_PATH),
    )


def successful_outcome(task_id: str) -> TaskOutcome:
    return TaskOutcome(
        task_id=task_id,
        goal_status=GoalStatus.STATUS_SUCCEEDED,
        goal_status_name="SUCCEEDED",
        final_state=5,
        final_state_name="SUCCEEDED",
        error_code=0,
        detail="navigation succeeded",
        attempts=1,
    )


def test_rejected_plan_never_initializes_ros(monkeypatch, capsys) -> None:
    from agent_gateway import mission_cli

    class RejectingModel:
        def plan(self, user_text, schema):
            raise ProviderError("unsupported mission")

    configure_local_schema(monkeypatch, mission_cli)
    monkeypatch.setattr(mission_cli, "create_mission_model", lambda _: RejectingModel())
    monkeypatch.setattr(
        mission_cli.rclpy,
        "init",
        lambda **_: pytest.fail("ROS must not initialize after planning rejection"),
    )

    assert mission_cli.main(["帮我拿水"]) == 2
    assert "Mission rejected" in capsys.readouterr().err


def test_declined_plan_never_initializes_ros(monkeypatch) -> None:
    from agent_gateway import mission_cli

    configure_local_schema(monkeypatch, mission_cli)
    monkeypatch.setattr("builtins.input", lambda _: "n")
    monkeypatch.setattr(
        mission_cli.rclpy,
        "init",
        lambda **_: pytest.fail("ROS must not initialize before confirmation"),
    )

    assert mission_cli.main(["先去充电桩，再去工作台"]) == 1


def test_yes_runs_two_steps_and_prints_stable_trace(monkeypatch, capsys) -> None:
    from agent_gateway import mission_cli

    requests = []
    lifecycle = []

    class FakeClient:
        def __init__(self, action_name):
            lifecycle.append(("client", action_name))

        def execute(self, request, feedback_callback=None):
            requests.append(request)
            return successful_outcome(request.task_id)

        def destroy_node(self):
            lifecycle.append(("destroy", ""))

    configure_local_schema(monkeypatch, mission_cli)
    monkeypatch.setattr(
        mission_cli, "create_mission_model", lambda _: FakeMissionModel()
    )
    monkeypatch.setattr(mission_cli, "ExecuteTaskClient", FakeClient)
    monkeypatch.setattr(
        mission_cli.rclpy, "init", lambda **_: lifecycle.append(("init", ""))
    )
    monkeypatch.setattr(
        mission_cli.rclpy, "shutdown", lambda: lifecycle.append(("shutdown", ""))
    )

    return_code = mission_cli.main(
        ["--yes", "--provider", "fake", "先去充电桩，再去工作台"]
    )

    output = capsys.readouterr().out
    assert return_code == 0
    assert [request.target for request in requests] == ["dock", "workbench"]
    assert lifecycle[0] == ("init", "")
    assert "Mission plan: dock -> workbench" in output
    assert output.count("Step result:") == 2
    assert "AI decision: continue" in output
    assert "Mission result: completed" in output
    assert "AI summary:" in output
