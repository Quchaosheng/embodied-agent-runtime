import argparse
import os
import sys

import rclpy

from agent_gateway.action_bridge import ActionBridgeError
from agent_gateway.action_bridge import ExecuteTaskClient
from agent_gateway.action_bridge import TaskProgress
from agent_gateway.provider import PROVIDER_CHOICES
from agent_gateway.provider import ProviderError
from agent_gateway.provider import create_model_provider
from agent_gateway.task_request import TaskRequestError
from agent_gateway.task_request import load_task_request_schema
from agent_gateway.task_request import parse_task_request


def _print_feedback(progress: TaskProgress) -> None:
    print(
        "Feedback: "
        f"state={progress.state_name} "
        f"attempt={progress.attempt} "
        f"distance_remaining={progress.distance_remaining:.2f} "
        f"detail={progress.detail}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Send an AI-normalized task to ExecuteTask")
    parser.add_argument("text", help="Natural-language robot task")
    parser.add_argument("--action-name", default="/execute_task")
    parser.add_argument(
        "--provider",
        choices=PROVIDER_CHOICES,
        default=os.environ.get("EMBODIED_AI_PROVIDER", "fake"),
    )
    arguments = parser.parse_args(argv)

    try:
        provider = create_model_provider(arguments.provider)
        raw_json = provider.generate_task(arguments.text, load_task_request_schema())
        request = parse_task_request(raw_json)
    except (ProviderError, TaskRequestError) as error:
        print(f"Gateway rejected request: {error}", file=sys.stderr)
        return 2

    print(
        "AI selected: "
        f"action={request.action} target={request.target} deadline_s={request.deadline_s}"
    )

    rclpy.init(args=None)
    client = ExecuteTaskClient(arguments.action_name)
    try:
        outcome = client.execute(request, feedback_callback=_print_feedback)
    except ActionBridgeError as error:
        print(f"Action bridge failed: {error}", file=sys.stderr)
        return_code = 3
    else:
        print(
            "Result: "
            f"task_id={outcome.task_id} "
            f"state={outcome.final_state_name} "
            f"goal_status={outcome.goal_status_name} "
            f"error_code={outcome.error_code} "
            f"attempts={outcome.attempts} "
            f"detail={outcome.detail}"
        )
        return_code = 0 if outcome.error_code == 0 else 1
    finally:
        client.destroy_node()
        rclpy.shutdown()
    return return_code
