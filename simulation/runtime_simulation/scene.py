from dataclasses import dataclass
import math
from pathlib import Path
from typing import Dict, List, Tuple

import yaml


CONTRACT_TARGETS = {"dock", "home", "workbench"}


class _UniqueKeyLoader(yaml.SafeLoader):
    pass


def _construct_unique_mapping(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise ValueError(f"duplicate scene key: {key}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


_UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
    _construct_unique_mapping,
)


@dataclass(frozen=True)
class Target:
    frame_id: str
    x: float
    y: float
    yaw: float


@dataclass(frozen=True)
class KeepoutZone:
    name: str
    polygon: Tuple[Tuple[float, float], ...]
    enforced: bool


@dataclass(frozen=True)
class Scene:
    targets: Dict[str, Target]
    keepout_zones: Tuple[KeepoutZone, ...]


def _finite_number(value: object, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{field} must be finite")
    return result


def load_scene(path: str) -> Scene:
    with Path(path).open(encoding="utf-8") as stream:
        root = yaml.load(stream, Loader=_UniqueKeyLoader)
    if not isinstance(root, dict) or set(root) != {"targets", "keepout_zones"}:
        raise ValueError("scene root must contain exactly targets and keepout_zones")

    target_data = root["targets"]
    if not isinstance(target_data, dict) or set(target_data) != CONTRACT_TARGETS:
        raise ValueError("scene targets must exactly match the task contract")

    targets = {}
    for name, raw_target in target_data.items():
        if not isinstance(raw_target, dict) or set(raw_target) != {"frame_id", "x", "y", "yaw"}:
            raise ValueError(f"target {name} must contain exactly frame_id, x, y, and yaw")
        if raw_target["frame_id"] != "map":
            raise ValueError(f"target {name} must use the map frame")
        yaw = _finite_number(raw_target["yaw"], f"target {name} yaw")
        if not -math.pi <= yaw <= math.pi:
            raise ValueError(f"target {name} yaw must be within [-pi, pi]")
        targets[name] = Target(
            frame_id="map",
            x=_finite_number(raw_target["x"], f"target {name} x"),
            y=_finite_number(raw_target["y"], f"target {name} y"),
            yaw=yaw,
        )

    zone_data = root["keepout_zones"]
    if not isinstance(zone_data, list):
        raise ValueError("keepout_zones must be a list")
    zones: List[KeepoutZone] = []
    observed_names = set()
    for raw_zone in zone_data:
        if not isinstance(raw_zone, dict) or set(raw_zone) != {"name", "polygon", "enforced"}:
            raise ValueError("each keepout zone must contain exactly name, polygon, and enforced")
        name = raw_zone["name"]
        if not isinstance(name, str) or not name or name in observed_names:
            raise ValueError("keepout zone names must be non-empty and unique")
        observed_names.add(name)
        raw_polygon = raw_zone["polygon"]
        if not isinstance(raw_polygon, list) or len(raw_polygon) < 3:
            raise ValueError(f"keepout zone {name} must have at least three vertices")
        polygon = []
        for index, raw_point in enumerate(raw_polygon):
            if not isinstance(raw_point, list) or len(raw_point) != 2:
                raise ValueError(f"keepout zone {name} vertex {index} must be [x, y]")
            polygon.append(
                (
                    _finite_number(raw_point[0], f"keepout zone {name} x"),
                    _finite_number(raw_point[1], f"keepout zone {name} y"),
                )
            )
        if not isinstance(raw_zone["enforced"], bool):
            raise ValueError(f"keepout zone {name} enforced must be boolean")
        zones.append(KeepoutZone(name, tuple(polygon), raw_zone["enforced"]))

    return Scene(targets, tuple(zones))
