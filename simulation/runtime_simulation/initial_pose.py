import math
import time

from geometry_msgs.msg import PoseWithCovarianceStamped
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy


def yaw_quaternion(yaw: float) -> tuple[float, float]:
    if not math.isfinite(yaw):
        raise ValueError("yaw must be finite")
    return math.sin(yaw / 2.0), math.cos(yaw / 2.0)


class InitialPosePublisher(Node):
    def __init__(self) -> None:
        super().__init__("runtime_initial_pose_publisher")
        self._x = float(self.declare_parameter("x", -2.0).value)
        self._y = float(self.declare_parameter("y", -0.5).value)
        self._yaw = float(self.declare_parameter("yaw", 0.0).value)
        self._max_wait_s = float(self.declare_parameter("max_wait_s", 45.0).value)
        if not all(math.isfinite(value) for value in (self._x, self._y, self._yaw)):
            raise ValueError("initial pose values must be finite")
        if self._max_wait_s <= 0.0 or not math.isfinite(self._max_wait_s):
            raise ValueError("max_wait_s must be finite and positive")

        qos = QoSProfile(depth=1)
        qos.reliability = ReliabilityPolicy.RELIABLE
        qos.durability = DurabilityPolicy.TRANSIENT_LOCAL
        self._publisher = self.create_publisher(PoseWithCovarianceStamped, "/initialpose", qos)
        self._started_at = time.monotonic()
        self._publication_count = 0
        self._timer = self.create_timer(1.0, self._publish_when_ready)

    def _message(self) -> PoseWithCovarianceStamped:
        message = PoseWithCovarianceStamped()
        message.header.frame_id = "map"
        message.header.stamp = self.get_clock().now().to_msg()
        message.pose.pose.position.x = self._x
        message.pose.pose.position.y = self._y
        message.pose.pose.orientation.z, message.pose.pose.orientation.w = yaw_quaternion(
            self._yaw
        )
        message.pose.covariance[0] = 0.25
        message.pose.covariance[7] = 0.25
        message.pose.covariance[35] = 0.0685
        return message

    def _publish_when_ready(self) -> None:
        if time.monotonic() - self._started_at > self._max_wait_s:
            self.get_logger().error("AMCL did not subscribe to /initialpose before timeout")
            rclpy.shutdown()
            return
        if self.count_subscribers("/initialpose") == 0:
            return
        self._publisher.publish(self._message())
        self._publication_count += 1
        self.get_logger().info(
            f"published AMCL initial pose ({self._x:.2f}, {self._y:.2f}, {self._yaw:.2f})"
        )
        if self._publication_count >= 3:
            rclpy.shutdown()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = InitialPosePublisher()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
