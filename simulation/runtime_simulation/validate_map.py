from ament_index_python.packages import get_package_share_directory

from runtime_simulation.map_validation import load_occupancy_map, validate_targets
from runtime_simulation.scene import load_scene


def main() -> None:
    scene_path = get_package_share_directory("runtime_simulation") + "/config/targets.yaml"
    map_path = get_package_share_directory("turtlebot3_navigation2") + "/map/map.yaml"
    scene = load_scene(scene_path)
    occupancy_map = load_occupancy_map(map_path)
    validate_targets(scene, occupancy_map)
    print("All named targets have at least 0.32 m free-space clearance.")
