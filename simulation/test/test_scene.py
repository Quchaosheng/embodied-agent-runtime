from pathlib import Path

import pytest

from runtime_simulation.scene import CONTRACT_TARGETS, load_scene


SCENE_PATH = Path(__file__).parents[1] / "config" / "targets.yaml"


def test_versioned_scene_matches_contract_and_spawn_pose():
    scene = load_scene(str(SCENE_PATH))

    assert set(scene.targets) == CONTRACT_TARGETS
    assert scene.targets["home"].x == -2.0
    assert scene.targets["home"].y == -0.5
    assert scene.keepout_zones[0].name == "restricted_area"
    assert scene.keepout_zones[0].enforced is False


def test_scene_rejects_non_map_target(tmp_path):
    invalid = tmp_path / "invalid.yaml"
    invalid.write_text(SCENE_PATH.read_text().replace("frame_id: map", "frame_id: odom", 1))

    with pytest.raises(ValueError, match="map frame"):
        load_scene(str(invalid))


def test_scene_rejects_missing_contract_target(tmp_path):
    invalid = tmp_path / "invalid.yaml"
    source = SCENE_PATH.read_text()
    start = source.index("  workbench:")
    end = source.index("  home:")
    invalid.write_text(source[:start] + source[end:])

    with pytest.raises(ValueError, match="exactly match"):
        load_scene(str(invalid))


def test_scene_rejects_ambiguous_keepout_status(tmp_path):
    invalid = tmp_path / "invalid.yaml"
    invalid.write_text(SCENE_PATH.read_text().replace("enforced: false", 'enforced: "false"'))

    with pytest.raises(ValueError, match="enforced must be boolean"):
        load_scene(str(invalid))


def test_scene_rejects_duplicate_keys(tmp_path):
    invalid = tmp_path / "invalid.yaml"
    invalid.write_text(SCENE_PATH.read_text().replace("    x: 0.0", "    x: 0.0\n    x: 1.0", 1))

    with pytest.raises(ValueError, match="duplicate scene key: x"):
        load_scene(str(invalid))
