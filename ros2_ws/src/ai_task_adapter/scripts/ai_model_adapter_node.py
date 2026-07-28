#!/usr/bin/env python3
"""Submit an allowlisted ExecuteWorkflow goal selected by an OpenAI-compatible model."""

from __future__ import annotations

import os
from pathlib import Path
import sys

module_dir = Path(__file__).resolve().parent
source_module_dir = module_dir.parent / 'ai_task_adapter'
sys.path.insert(0, str(source_module_dir if source_module_dir.is_dir() else module_dir))

from action_msgs.msg import GoalStatus  # noqa: E402
from model_contract import (  # noqa: E402
    ModelPlanError,
    ModelTransportError,
    parse_model_plan,
    request_chat_completion,
)

import rclpy  # noqa: E402
from rclpy.action import ActionClient  # noqa: E402
from rclpy.node import Node  # noqa: E402

from robot_task_interfaces.action import ExecuteWorkflow  # noqa: E402


class AiModelAdapterNode(Node):

    def __init__(self) -> None:
        super().__init__('ai_model_adapter')
        self._request = self.declare_parameter('request', 'Go to dock_a.').value
        self._request_id = self.declare_parameter('request_id', 'model_demo_1').value
        self._task_id = self.declare_parameter('task_id', 'model_task_1').value
        self._mode = self.declare_parameter('mode', 'disabled').value
        self._endpoint = self.declare_parameter('model_endpoint', '').value
        self._model_name = self.declare_parameter('model_name', '').value
        self._api_key_env = self.declare_parameter('api_key_env', 'OPENAI_API_KEY').value
        self._timeout_sec = float(self.declare_parameter('model_timeout_sec', 10.0).value)
        self._action_timeout_sec = float(
            self.declare_parameter('action_result_timeout_sec', 15.0).value)
        self._max_duration_ms = int(self.declare_parameter('max_duration_ms', 5000).value)
        self._workflows = list(self.declare_parameter(
            'workflow_ids', ['single_task', 'ready_then_task']).value)
        self._targets = list(self.declare_parameter('targets', ['dock_a', 'home']).value)
        self._client = ActionClient(self, ExecuteWorkflow, 'execute_workflow')

    def run(self) -> int:
        if self._mode != 'openai_compatible':
            self.get_logger().error(
                'mode must be openai_compatible; model calls are disabled by default')
            return 2
        if not self._request or not self._request_id or not self._task_id:
            self.get_logger().error('request, request_id, and task_id must not be empty')
            return 2
        if (
            not self._endpoint or not self._model_name or
            self._timeout_sec <= 0 or self._action_timeout_sec <= 0
        ):
            self.get_logger().error(
                'model_endpoint, model_name, and positive timeouts are required')
            return 2
        if self._max_duration_ms <= 0 or not self._workflows or not self._targets:
            self.get_logger().error('max_duration_ms and non-empty allowlists are required')
            return 2

        try:
            content = self._request_model()
            plan = parse_model_plan(content, self._workflows, self._targets, self._max_duration_ms)
        except (ModelPlanError, ModelTransportError) as error:
            self.get_logger().error(f'model plan rejected: {error}')
            return 2

        self.get_logger().info(
            'validated model plan '
            f'workflow_id={plan.workflow_id} target_id={plan.target_id} '
            f'duration_ms={plan.duration_ms}'
        )
        if not self._client.wait_for_server(timeout_sec=5.0):
            self.get_logger().error('ExecuteWorkflow server not available after 5 seconds')
            return 1

        goal = ExecuteWorkflow.Goal()
        goal.request_id = self._request_id
        goal.task_id = self._task_id
        goal.workflow_id = plan.workflow_id
        goal.target_id = plan.target_id
        goal.allowed_duration.sec = plan.duration_ms // 1000
        goal.allowed_duration.nanosec = (plan.duration_ms % 1000) * 1_000_000
        send_future = self._client.send_goal_async(goal)
        rclpy.spin_until_future_complete(self, send_future, timeout_sec=5.0)
        if not send_future.done():
            self.get_logger().error('ExecuteWorkflow goal response timed out')
            return 4
        try:
            goal_handle = send_future.result()
        except Exception as error:
            self.get_logger().error(f'ExecuteWorkflow goal request failed: {error}')
            return 4
        if goal_handle is None or not goal_handle.accepted:
            self.get_logger().error('ExecuteWorkflow goal rejected')
            return 2

        result_future = goal_handle.get_result_async()
        rclpy.spin_until_future_complete(
            self, result_future, timeout_sec=self._action_timeout_sec)
        if not result_future.done():
            self.get_logger().error('ExecuteWorkflow result timed out; requesting cancellation')
            cancel_future = goal_handle.cancel_goal_async()
            rclpy.spin_until_future_complete(self, cancel_future, timeout_sec=2.0)
            return 4
        try:
            result = result_future.result()
        except Exception as error:
            self.get_logger().error(f'ExecuteWorkflow result failed: {error}')
            return 4
        if result is None:
            self.get_logger().error('ExecuteWorkflow returned no result')
            return 4
        self.get_logger().info(
            f'ExecuteWorkflow outcome={result.result.outcome} '
            f'error_code={result.result.error_code} message={result.result.message}'
        )
        completed = (
            result.status == GoalStatus.STATUS_SUCCEEDED and
            result.result.outcome == ExecuteWorkflow.Result.COMPLETED
        )
        return 0 if completed else 4

    def _request_model(self) -> str:
        api_key = ''
        if self._api_key_env:
            api_key = os.environ.get(self._api_key_env, '')
            if not api_key:
                raise ModelTransportError(
                    f'required API key environment variable is empty: {self._api_key_env}')
        return request_chat_completion(
            self._endpoint,
            self._model_name,
            api_key,
            self._request,
            self._workflows,
            self._targets,
            self._max_duration_ms,
            self._timeout_sec,
        )


def main() -> int:
    rclpy.init()
    node = AiModelAdapterNode()
    try:
        return node.run()
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
