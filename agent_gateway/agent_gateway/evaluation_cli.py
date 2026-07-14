import argparse
import os
import sys

from agent_gateway.intent_evaluation import EvaluationConfigError
from agent_gateway.intent_evaluation import evaluate_intents
from agent_gateway.intent_evaluation import format_intent_report
from agent_gateway.intent_evaluation import load_intent_cases
from agent_gateway.provider import PROVIDER_CHOICES
from agent_gateway.provider import ProviderError
from agent_gateway.provider import create_model_provider
from agent_gateway.task_request import load_task_request_schema


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Evaluate a model provider against fixed Chinese intent cases"
    )
    parser.add_argument("--cases", help="Optional path to an evaluation JSON file")
    parser.add_argument(
        "--provider",
        choices=PROVIDER_CHOICES,
        default=os.environ.get("EMBODIED_AI_PROVIDER", "fake"),
    )
    arguments = parser.parse_args(argv)

    try:
        schema = load_task_request_schema()
        cases = load_intent_cases(schema, arguments.cases)
        provider = create_model_provider(arguments.provider)
        report = evaluate_intents(provider, cases, schema)
    except (EvaluationConfigError, ProviderError) as error:
        print(f"Intent evaluation could not start: {error}", file=sys.stderr)
        return 2

    print(format_intent_report(report))
    return 0 if report.passed == report.total else 1
