import subprocess
from pathlib import Path

import pytest


VALIDATOR = Path(__file__).parents[2] / "scripts" / "validate_nav2_smoke_mode.sh"


@pytest.mark.parametrize("mode", ["direct", "mission"])
def test_accepts_supported_nav2_smoke_modes(mode: str) -> None:
    assert subprocess.run(["bash", str(VALIDATOR), mode], check=False).returncode == 0


def test_rejects_unknown_nav2_smoke_mode() -> None:
    assert (
        subprocess.run(["bash", str(VALIDATOR), "anything"], check=False).returncode
        != 0
    )
