import pathlib
import sqlite3
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

from robot_task_interfaces.msg import TaskEvent


class LateJoinTaskEventsTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        rclpy.shutdown()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.database = pathlib.Path(self.directory.name) / 'history.sqlite3'
        self.node = Node('late_join_task_event_source')
        # The QoS the Task Executor publishes terminal events with.
        self.publisher = self.node.create_publisher(
            TaskEvent,
            '/runtime/task_events',
            QoSProfile(
                depth=128,
                reliability=ReliabilityPolicy.RELIABLE,
                durability=DurabilityPolicy.TRANSIENT_LOCAL,
            ),
        )
        self.process = None

    def tearDown(self):
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=2)
        self.node.destroy_node()
        self.directory.cleanup()

    def recorded_task_ids(self):
        try:
            with sqlite3.connect(f'file:{self.database}?mode=ro', uri=True) as connection:
                return [row[0] for row in connection.execute('SELECT task_id FROM task_runs')]
        except sqlite3.OperationalError:
            # The node has not created the database or the table yet.
            return []

    def test_event_published_before_history_started_is_recorded(self):
        event = TaskEvent()
        event.stamp = self.node.get_clock().now().to_msg()
        event.task_id = 'finished-before-history'
        event.target_id = 'dock_a'
        event.action_status = TaskEvent.SUCCEEDED
        event.duration_ms = 12
        event.message = 'retained terminal event'
        self.publisher.publish(event)

        prefix = get_package_prefix('runtime_history')
        self.process = subprocess.Popen(
            [
                f'{prefix}/lib/runtime_history/runtime_history_node',
                '--ros-args',
                '-p',
                f'database_path:={self.database}',
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        deadline = time.monotonic() + 10.0
        recorded = []
        while time.monotonic() < deadline and not recorded:
            self.assertIsNone(self.process.poll(), 'runtime_history_node exited early')
            recorded = self.recorded_task_ids()
            time.sleep(0.05)
        self.assertEqual(recorded, ['finished-before-history'])


if __name__ == '__main__':
    unittest.main()
