import argparse
import os
import sys

from agent_gateway.provider import PROVIDER_CHOICES
from agent_gateway.provider import ProviderError
from agent_gateway.provider import create_model_provider
from agent_gateway.task_request import TaskRequestError
from agent_gateway.task_request import load_task_request_schema
from agent_gateway.task_request import parse_task_request


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check one model request without sending a ROS Action"
    )
    parser.add_argument("text", help="Natural-language robot task")
    parser.add_argument(
        "--provider",
        choices=PROVIDER_CHOICES,
        default=os.environ.get("EMBODIED_AI_PROVIDER", "fake"),
    )
    arguments = parser.parse_args(argv)

    try:
        schema = load_task_request_schema()
        provider = create_model_provider(arguments.provider)
        raw_json = provider.generate_task(arguments.text, schema)
        request = parse_task_request(raw_json)
    except (ProviderError, TaskRequestError) as error:
        print(f"Provider probe rejected request: {error}", file=sys.stderr)
        return 2

    print(
        "Provider probe passed: "
        f"action={request.action} target={request.target} "
        f"deadline_s={request.deadline_s}"
    )
    print("No ROS Action was sent.")
    return 0
