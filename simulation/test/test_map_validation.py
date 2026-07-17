from pathlib import Path

import pytest

from runtime_simulation.map_validation import load_occupancy_map, validate_targets
from runtime_simulation.scene import Scene, Target


def _write_map(tmp_path: Path, pixels: bytes) -> Path:
    image = tmp_path / "map.pgm"
    image.write_bytes(b"P5\n# test map\n5 5\n255\n" + pixels)
    metadata = tmp_path / "map.yaml"
    metadata.write_text(
        "image: map.pgm\n"
        "resolution: 1.0\n"
        "origin: [0.0, 0.0, 0.0]\n"
        "negate: 0\n"
        "occupied_thresh: 0.65\n"
        "free_thresh: 0.196\n"
    )
    return metadata


def _scene_at(x: float, y: float) -> Scene:
    target = Target("map", x, y, 0.0)
    return Scene({"dock": target, "home": target, "workbench": target}, ())


def test_targets_with_clearance_are_accepted(tmp_path):
    occupancy_map = load_occupancy_map(str(_write_map(tmp_path, bytes([254] * 25))))

    validate_targets(_scene_at(2.5, 2.5), occupancy_map, clearance=1.0)


def test_occupied_neighbor_is_rejected(tmp_path):
    pixels = bytearray([254] * 25)
    pixels[2 * 5 + 3] = 0
    occupancy_map = load_occupancy_map(str(_write_map(tmp_path, bytes(pixels))))

    with pytest.raises(ValueError, match="dock, home, workbench"):
        validate_targets(_scene_at(2.5, 2.5), occupancy_map, clearance=1.0)


def test_target_outside_map_is_rejected(tmp_path):
    occupancy_map = load_occupancy_map(str(_write_map(tmp_path, bytes([254] * 25))))

    with pytest.raises(ValueError, match="outside"):
        validate_targets(_scene_at(-1.0, 2.0), occupancy_map, clearance=0.1)
