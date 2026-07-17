import argparse
import os
import sys

import rclpy

from agent_gateway.action_bridge import ExecuteTaskClient
from agent_gateway.action_bridge import TaskProgress
from agent_gateway.mission_plan import MissionPlanError
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan
from agent_gateway.mission_provider import MISSION_PROVIDER_CHOICES
from agent_gateway.mission_provider import create_mission_model
from agent_gateway.mission_runner import MissionRunner
from agent_gateway.provider import ProviderError


def _print_feedback(progress: TaskProgress) -> None:
    print(
        "Feedback: "
        f"state={progress.state_name} "
        f"attempt={progress.attempt} "
        f"distance_remaining={progress.distance_remaining:.2f} "
        f"detail={progress.detail}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Plan and execute a bounded named-target robot mission"
    )
    parser.add_argument("text", help="Natural-language robot mission")
    parser.add_argument("--action-name", default="/execute_task")
    parser.add_argument("--yes", action="store_true", help="Skip confirmation")
    parser.add_argument(
        "--provider",
        choices=MISSION_PROVIDER_CHOICES,
        default=os.environ.get("EMBODIED_AI_PROVIDER", "fake"),
    )
    arguments = parser.parse_args(argv)

    try:
        model = create_mission_model(arguments.provider)
        raw_plan = model.plan(arguments.text, load_mission_plan_schema())
        plan = parse_mission_plan(raw_plan)
    except (ProviderError, MissionPlanError) as error:
        print(f"Mission rejected: {error}", file=sys.stderr)
        return 2

    print("Mission plan: " + " -> ".join(step.target for step in plan.steps))
    if not arguments.yes and input("Execute mission? [y/N] ").strip().lower() != "y":
        print("Mission cancelled before ROS execution.")
        return 1

    rclpy.init(args=None)
    client = ExecuteTaskClient(arguments.action_name)
    try:
        runner = MissionRunner(
            model,
            lambda request, feedback_callback: client.execute(
                request, feedback_callback=feedback_callback
            ),
        )
        result = runner.run(plan, feedback_callback=_print_feedback)
    finally:
        client.destroy_node()
        rclpy.shutdown()

    for record in result.trace.records:
        if record.outcome is None:
            print(f"Step result: target={record.request.target} error={record.error}")
        else:
            print(
                "Step result: "
                f"target={record.request.target} "
                f"state={record.outcome.final_state_name} "
                f"goal_status={record.outcome.goal_status_name} "
                f"error_code={record.outcome.error_code} "
                f"attempts={record.outcome.attempts}"
            )
    for decision in result.trace.decisions:
        print(f"AI decision: {decision}")
    print(f"Mission result: {result.trace.final_reason}")
    print(f"AI summary: {result.summary}")
    return 0 if result.trace.final_reason == "completed" else 1
