import math

from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import Point
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from runtime_simulation.scene import load_scene
from visualization_msgs.msg import Marker, MarkerArray


class SceneMarkerPublisher(Node):
    def __init__(self) -> None:
        super().__init__("runtime_scene_marker_publisher")
        default_path = get_package_share_directory("runtime_simulation") + "/config/targets.yaml"
        scene_path = str(self.declare_parameter("scene_path", default_path).value)
        self._scene = load_scene(scene_path)
        qos = QoSProfile(depth=1)
        qos.reliability = ReliabilityPolicy.RELIABLE
        qos.durability = DurabilityPolicy.TRANSIENT_LOCAL
        self._publisher = self.create_publisher(MarkerArray, "/runtime/scene_markers", qos)
        self._timer = self.create_timer(2.0, self._publish)
        self._publish()

    def _publish(self) -> None:
        stamp = self.get_clock().now().to_msg()
        array = MarkerArray()
        for index, (name, target) in enumerate(sorted(self._scene.targets.items())):
            arrow = Marker()
            arrow.header.frame_id = target.frame_id
            arrow.header.stamp = stamp
            arrow.ns = "named_targets"
            arrow.id = index * 2
            arrow.type = Marker.ARROW
            arrow.action = Marker.ADD
            arrow.pose.position.x = target.x
            arrow.pose.position.y = target.y
            arrow.pose.position.z = 0.08
            arrow.pose.orientation.z = math.sin(target.yaw / 2.0)
            arrow.pose.orientation.w = math.cos(target.yaw / 2.0)
            arrow.scale.x = 0.45
            arrow.scale.y = 0.10
            arrow.scale.z = 0.10
            arrow.color.r = 0.10
            arrow.color.g = 0.75
            arrow.color.b = 0.95
            arrow.color.a = 0.95
            array.markers.append(arrow)

            label = Marker()
            label.header = arrow.header
            label.ns = "target_labels"
            label.id = index * 2 + 1
            label.type = Marker.TEXT_VIEW_FACING
            label.action = Marker.ADD
            label.pose.position.x = target.x
            label.pose.position.y = target.y
            label.pose.position.z = 0.55
            label.pose.orientation.w = 1.0
            label.scale.z = 0.28
            label.color.r = 0.95
            label.color.g = 0.95
            label.color.b = 0.95
            label.color.a = 1.0
            label.text = name
            array.markers.append(label)

        marker_id = len(self._scene.targets) * 2
        for zone in self._scene.keepout_zones:
            outline = Marker()
            outline.header.frame_id = "map"
            outline.header.stamp = stamp
            outline.ns = "keepout_zones"
            outline.id = marker_id
            marker_id += 1
            outline.type = Marker.LINE_STRIP
            outline.action = Marker.ADD
            outline.pose.orientation.w = 1.0
            outline.scale.x = 0.08
            outline.color.r = 1.0
            outline.color.g = 0.1 if zone.enforced else 0.55
            outline.color.b = 0.05
            outline.color.a = 0.95
            for x, y in (*zone.polygon, zone.polygon[0]):
                point = Point()
                point.x = x
                point.y = y
                point.z = 0.05
                outline.points.append(point)
            array.markers.append(outline)

        self._publisher.publish(array)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = SceneMarkerPublisher()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
