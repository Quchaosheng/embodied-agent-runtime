"""
Dispatch budget tests for the model adapter submission path.

These drive the real `AiModelAdapterNode.run()` with stubbed ROS clients and a
controlled clock, so the assertions are about the adapter's own decisions rather
than about a real Action server.
"""

import os
from pathlib import Path
import sys
import types
import unittest
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1] / 'scripts'
sys.path.insert(0, str(SCRIPT_DIR))


def _add_ros_python_path():
    """Make rclpy/action_msgs importable when ctest replaces PYTHONPATH."""
    try:
        import action_msgs  # noqa: F401
        return
    except ImportError:
        pass
    prefixes = [os.environ.get('AMENT_PREFIX_PATH', ''),
                os.environ.get('COLCON_PREFIX_PATH', ''),
                os.environ.get('ROS_PREFIX_PATH', ''),
                os.environ.get('ROS_DISTRO_ROOT', '')]
    candidates = [Path(value) for prefix in prefixes
                  for value in prefix.split(os.pathsep) if value]
    candidates.append(Path('/opt/ros') / os.environ.get('ROS_DISTRO', 'jazzy'))
    for prefix in candidates:
        py = f'python{sys.version_info.major}.{sys.version_info.minor}'
        site = prefix / 'lib' / py / 'site-packages'
        if site.is_dir() and str(site) not in sys.path:
            sys.path.append(str(site))


_add_ros_python_path()

import ai_model_adapter_node as adapter_module  # noqa: E402


class FakeLogger:

    def error(self, message):
        pass

    def info(self, message):
        pass


class FakeFuture:

    def __init__(self, done=True, result=None):
        self._done = done
        self._result = result
        self._callbacks = []

    def done(self):
        return self._done

    def result(self):
        return self._result

    def add_done_callback(self, callback):
        self._callbacks.append(callback)
        if self._done:
            callback(self)


class FakeGoalHandle:

    accepted = True

    def __init__(self):
        self.cancelled = 0
        self.result = types.SimpleNamespace(
            status=adapter_module.GoalStatus.STATUS_SUCCEEDED,
            result=types.SimpleNamespace(outcome=1, error_code=0, message='done'),
        )

    def get_result_async(self):
        return FakeFuture(done=True, result=self.result)

    def cancel_goal_async(self):
        self.cancelled += 1
        return FakeFuture(done=True, result=None)


class LateAcceptFuture(FakeFuture):
    """A send future that stays pending until the test resolves it."""

    def __init__(self):
        super().__init__(done=False)
        self._callbacks = []

    def add_done_callback(self, callback):
        self._callbacks.append(callback)

    def resolve(self, handle):
        self._result = handle
        self._done = True
        for callback in self._callbacks:
            callback(self)


class FakeActionClient:

    def __init__(self, wait_ok=True, wait_cost_ns=0, send_done=True):
        self.wait_ok = wait_ok
        self.wait_cost_ns = wait_cost_ns
        self.send_done = send_done
        self.wait_timeout_sec = None
        self.sent = 0
        self.last_goal = None
        self.last_handle = None
        self.pending_future = None

    def wait_for_server(self, timeout_sec=None):
        self.wait_timeout_sec = timeout_sec
        if self.wait_cost_ns:
            clock.advance(self.wait_cost_ns)
        return self.wait_ok

    def send_goal_async(self, goal):
        self.sent += 1
        self.last_goal = goal
        if self.send_done:
            self.last_handle = FakeGoalHandle()
            return FakeFuture(done=True, result=self.last_handle)
        self.pending_future = LateAcceptFuture()
        return self.pending_future


class FakeClock:

    def __init__(self):
        self.now_ns = 0

    def monotonic_ns(self):
        return self.now_ns

    def advance(self, delta_ns):
        self.now_ns += delta_ns


clock = FakeClock()


def build_node(**overrides):
    node = adapter_module.AiModelAdapterNode.__new__(adapter_module.AiModelAdapterNode)
    node._request = 'Go to dock_a.'
    node._request_id = 'model-test-1'
    node._task_id = 'model-task-1'
    node._mode = 'mock'
    node._api_style = 'chat_completions'
    node._endpoint = ''
    node._model_name = ''
    node._api_key_env = ''
    node._timeout_sec = 10.0
    node._server_wait_sec = 2.0
    node._observation_timestamp_ns = 0
    node._observation_ttl_ms = 5000
    node._inference_deadline_ms = 2500
    node._output_version = 'workflow-plan/v1'
    node._record_path = ''
    node._replay_path = ''
    node._metrics_path = ''
    node._fault_mode = 'none'
    node._mock_response = (
        '{"workflow_id":"single_task","target_id":"dock_a","duration_ms":1000}'
    )
    node._workflows = ['single_task', 'ready_then_task']
    node._targets = ['dock_a', 'home']
    node._max_duration_ms = 5000
    node._action_timeout_sec = 5.0
    node._admission = adapter_module.ModelAdmission(
        dedup_window_ms=5000,
        failure_window_ms=60000,
        max_failures=3,
        max_future_skew_ms=1000,
    )
    node._metrics = adapter_module.ModelRuntimeMetrics()
    node._client = FakeActionClient()
    node._awaiting_acceptance = False
    node.get_logger = lambda: FakeLogger()
    for key, value in overrides.items():
        setattr(node, key, value)
    return node


class ModelDispatchBudgetTest(unittest.TestCase):

    def setUp(self):
        self._patches = [
            mock.patch.object(adapter_module.time, 'monotonic_ns', clock.monotonic_ns),
            mock.patch.object(adapter_module.rclpy, 'spin_until_future_complete',
                              lambda *args, **kwargs: None),
        ]
        for patch in self._patches:
            patch.start()
        clock.now_ns = 0

    def tearDown(self):
        for patch in reversed(self._patches):
            patch.stop()

    def test_server_wait_is_capped_by_the_remaining_budget(self):
        client = FakeActionClient()
        node = build_node(_client=client, _server_wait_sec=2.0)
        node.run()
        self.assertIsNotNone(client.wait_timeout_sec)
        self.assertLessEqual(client.wait_timeout_sec, 2.0)
        self.assertGreater(client.wait_timeout_sec, 0.0)
        self.assertEqual(client.sent, 1)

    def test_expired_plan_is_not_dispatched_after_the_server_appears(self):
        # Default deadline is 2.5 s; the server only shows up at 3 s.
        client = FakeActionClient(wait_ok=True, wait_cost_ns=3_000_000_000)
        node = build_node(_client=client)
        exit_code = node.run()
        self.assertNotEqual(exit_code, 0)
        self.assertEqual(client.sent, 0)

    def test_unavailable_server_reports_failure_without_dispatch(self):
        client = FakeActionClient(wait_ok=False)
        node = build_node(_client=client)
        exit_code = node.run()
        self.assertEqual(exit_code, 1)
        self.assertEqual(client.sent, 0)


class ModelLateAcceptanceTest(unittest.TestCase):
    """A goal accepted after the response timeout must not run unattended."""

    def setUp(self):
        self._patches = [
            mock.patch.object(adapter_module.time, 'monotonic_ns', clock.monotonic_ns),
            mock.patch.object(adapter_module.rclpy, 'spin_until_future_complete',
                              lambda *args, **kwargs: None),
        ]
        for patch in self._patches:
            patch.start()
        clock.now_ns = 0

    def tearDown(self):
        for patch in reversed(self._patches):
            patch.stop()

    def test_late_acceptance_after_response_timeout_is_cancelled(self):
        client = FakeActionClient(send_done=False)
        node = build_node(_client=client)
        exit_code = node.run()
        self.assertEqual(exit_code, 4)
        self.assertEqual(client.sent, 1)

        # The server accepts after run() already reported the timeout.
        accepted = FakeGoalHandle()
        client.pending_future.resolve(accepted)
        self.assertEqual(accepted.cancelled, 1)

    def test_acceptance_before_the_timeout_is_not_cancelled(self):
        client = FakeActionClient()
        node = build_node(_client=client)
        node.run()
        self.assertEqual(client.last_handle.cancelled, 0)

    def test_rejected_late_response_is_not_cancelled(self):
        class RejectedHandle(FakeGoalHandle):
            accepted = False

        client = FakeActionClient(send_done=False)
        node = build_node(_client=client)
        node.run()
        rejected = RejectedHandle()
        client.pending_future.resolve(rejected)
        self.assertEqual(rejected.cancelled, 0)


if __name__ == '__main__':
    unittest.main()
