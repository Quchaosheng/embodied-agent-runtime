import argparse
import sys

from agent_gateway.mission_evaluation import EvaluationConfigError
from agent_gateway.mission_evaluation import evaluate_missions
from agent_gateway.mission_evaluation import format_mission_report
from agent_gateway.mission_evaluation import load_mission_cases
from agent_gateway.mission_plan import load_mission_plan_schema
from agent_gateway.mission_provider import MISSION_PROVIDER_CHOICES
from agent_gateway.mission_provider import create_mission_model
from agent_gateway.provider import ProviderError


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Evaluate bounded mission planning")
    parser.add_argument("--cases")
    parser.add_argument(
        "--provider", choices=MISSION_PROVIDER_CHOICES, default="fake"
    )
    arguments = parser.parse_args(argv)
    try:
        schema = load_mission_plan_schema()
        cases = load_mission_cases(schema, arguments.cases)
        model = create_mission_model(arguments.provider)
        report = evaluate_missions(model, cases, schema)
    except (EvaluationConfigError, ProviderError) as error:
        print(f"Mission evaluation failed: {error}", file=sys.stderr)
        return 2
    print(format_mission_report(report))
    return 0 if report.passed == report.total else 1
