import argparse
import sys

from agent_gateway.mission_plan import MissionPlanError
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_plan import parse_mission_plan
from agent_gateway.mission_provider import MISSION_PROVIDER_CHOICES
from agent_gateway.mission_provider import create_mission_model
from agent_gateway.provider import ProviderError


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Plan one mission without initializing ROS"
    )
    parser.add_argument("text", help="Natural-language robot mission")
    parser.add_argument(
        "--provider", choices=MISSION_PROVIDER_CHOICES, default="fake"
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
    return 0
