from pathlib import Path

from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan


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


def test_probe_prints_plan_without_ros(monkeypatch, capsys) -> None:
    from agent_gateway import mission_probe

    configure_local_schema(monkeypatch, mission_probe)

    assert mission_probe.main(["--provider", "fake", "先去充电桩，再去工作台"]) == 0
    assert "Mission plan: dock -> workbench" in capsys.readouterr().out


def test_probe_rejects_unsupported_mission(monkeypatch, capsys) -> None:
    from agent_gateway import mission_probe

    configure_local_schema(monkeypatch, mission_probe)

    assert mission_probe.main(["--provider", "fake", "帮我拿水"]) == 2
    assert "Mission rejected" in capsys.readouterr().err
