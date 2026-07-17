import subprocess
from pathlib import Path

import pytest


VALIDATOR = Path(__file__).parents[2] / "scripts" / "validate_nav2_smoke_mode.sh"
ENVIRONMENT_CHECK = (
    Path(__file__).parents[2] / "scripts" / "check_nav2_sim_environment.sh"
)
SIMULATION_README = Path(__file__).parents[1] / "README.md"


def test_environment_check_covers_robot_state_publisher() -> None:
    check_text = ENVIRONMENT_CHECK.read_text(encoding="utf-8")
    readme_text = SIMULATION_README.read_text(encoding="utf-8")

    assert "robot_state_publisher" in check_text
    assert "ros-jazzy-robot-state-publisher" in check_text
    assert "ros-jazzy-robot-state-publisher" in readme_text


@pytest.mark.parametrize("mode", ["direct", "mission"])
def test_accepts_supported_nav2_smoke_modes(mode: str) -> None:
    assert subprocess.run(["bash", str(VALIDATOR), mode], check=False).returncode == 0


def test_rejects_unknown_nav2_smoke_mode() -> None:
    assert (
        subprocess.run(["bash", str(VALIDATOR), "anything"], check=False).returncode
        != 0
    )
