import subprocess
from pathlib import Path


CHECKER = Path(__file__).parents[2] / "scripts" / "is_lifecycle_active.sh"


def check_state(state: str) -> int:
    return subprocess.run(
        ["bash", str(CHECKER)],
        input=state,
        text=True,
        check=False,
    ).returncode


def test_active_lifecycle_state_is_accepted():
    assert check_state("active [3]\n") == 0


def test_inactive_lifecycle_state_is_rejected():
    assert check_state("inactive [2]\n") != 0
