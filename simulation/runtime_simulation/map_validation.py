from dataclasses import dataclass
import math
from pathlib import Path
from typing import Tuple
from runtime_simulation.scene import Scene
import yaml


@dataclass(frozen=True)
class OccupancyMap:
    width: int
    height: int
    resolution: float
    origin_x: float
    origin_y: float
    negate: bool
    free_thresh: float
    pixels: bytes

    def is_free_with_clearance(self, x: float, y: float, clearance: float) -> bool:
        radius_cells = math.ceil(clearance / self.resolution)
        center_column = math.floor((x - self.origin_x) / self.resolution)
        center_from_bottom = math.floor((y - self.origin_y) / self.resolution)
        center_row = self.height - 1 - center_from_bottom

        for row_offset in range(-radius_cells, radius_cells + 1):
            for column_offset in range(-radius_cells, radius_cells + 1):
                if math.hypot(row_offset, column_offset) * self.resolution > clearance:
                    continue
                row = center_row + row_offset
                column = center_column + column_offset
                if row < 0 or row >= self.height or column < 0 or column >= self.width:
                    return False
                value = self.pixels[row * self.width + column]
                occupancy = value / 255.0 if self.negate else (255 - value) / 255.0
                if occupancy >= self.free_thresh:
                    return False
        return True


def _pgm_token(stream) -> bytes:
    token = bytearray()
    while True:
        character = stream.read(1)
        if not character:
            raise ValueError("unexpected end of PGM header")
        if character == b"#":
            stream.readline()
            continue
        if not character.isspace():
            token.extend(character)
            break
    while True:
        character = stream.read(1)
        if not character or character.isspace():
            return bytes(token)
        token.extend(character)


def _load_pgm(path: Path) -> Tuple[int, int, bytes]:
    with path.open("rb") as stream:
        if _pgm_token(stream) != b"P5":
            raise ValueError("occupancy image must be a binary P5 PGM")
        width = int(_pgm_token(stream))
        height = int(_pgm_token(stream))
        maximum = int(_pgm_token(stream))
        if width <= 0 or height <= 0 or maximum != 255:
            raise ValueError("PGM dimensions must be positive and max value must be 255")
        pixels = stream.read()
    if len(pixels) != width * height:
        raise ValueError("PGM pixel count does not match its dimensions")
    return width, height, pixels


def load_occupancy_map(path: str) -> OccupancyMap:
    yaml_path = Path(path)
    with yaml_path.open(encoding="utf-8") as stream:
        metadata = yaml.safe_load(stream)
    required = {"image", "resolution", "origin", "negate", "occupied_thresh", "free_thresh"}
    if not isinstance(metadata, dict) or set(metadata) != required:
        raise ValueError("map YAML must contain the standard six occupancy fields")
    resolution = float(metadata["resolution"])
    origin = metadata["origin"]
    free_thresh = float(metadata["free_thresh"])
    if not math.isfinite(resolution) or resolution <= 0.0:
        raise ValueError("map resolution must be finite and positive")
    if not isinstance(origin, list) or len(origin) != 3:
        raise ValueError("map origin must contain x, y, and yaw")
    if not 0.0 < free_thresh < 1.0:
        raise ValueError("map free_thresh must be within (0, 1)")
    image_path = Path(metadata["image"])
    if not image_path.is_absolute():
        image_path = yaml_path.parent / image_path
    width, height, pixels = _load_pgm(image_path)
    return OccupancyMap(
        width=width,
        height=height,
        resolution=resolution,
        origin_x=float(origin[0]),
        origin_y=float(origin[1]),
        negate=bool(metadata["negate"]),
        free_thresh=free_thresh,
        pixels=pixels,
    )


def validate_targets(scene: Scene, occupancy_map: OccupancyMap, clearance: float = 0.32) -> None:
    if not math.isfinite(clearance) or clearance <= 0.0:
        raise ValueError("target clearance must be finite and positive")
    blocked = [
        name
        for name, target in scene.targets.items()
        if not occupancy_map.is_free_with_clearance(target.x, target.y, clearance)
    ]
    if blocked:
        detail = "targets are occupied, unknown, outside the map, or lack clearance: "
        raise ValueError(detail + ", ".join(blocked))
