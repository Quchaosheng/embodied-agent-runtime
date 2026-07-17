import importlib.util
from pathlib import Path

import yaml
from launch import LaunchContext


LAUNCH_FILE = Path(__file__).parents[1] / "launch" / "runtime_nav2_sim.launch.py"


def test_collision_monitor_timeout_allows_for_five_hertz_scan_jitter(tmp_path):
    spec = importlib.util.spec_from_file_location("runtime_nav2_sim_launch", LAUNCH_FILE)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)

    source = tmp_path / "nav2.yaml"
    source.write_text(
        "collision_monitor:\n"
        "  ros__parameters:\n"
        "    scan:\n"
        "      source_timeout: 0.2\n"
    )

    rewritten_path = module._nav2_parameters(str(source)).perform(LaunchContext())
    rewritten = yaml.safe_load(Path(rewritten_path).read_text())

    assert rewritten["collision_monitor"]["ros__parameters"]["scan"]["source_timeout"] == 0.5
