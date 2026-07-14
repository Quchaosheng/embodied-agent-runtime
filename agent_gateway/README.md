# Agent Gateway

agent_gateway is the untrusted model-output boundary for Embodied Agent Runtime.

## Implemented behavior

The package parses JSON text, rejects duplicate fields, validates the object
against task_contract/schema/task_request.schema.json, and returns an immutable
NormalizedTaskRequest.

It also provides:

- a provider-independent ExecuteTask Action Client.
- generated task IDs when callers omit one.
- stable feedback and terminal outcome objects.
- an offline FakeModelProvider for three approved targets.
- official OpenAI and OpenAI-compatible relay profiles configured by environment.
- a provider probe that never sends a ROS Action.
- a fixed 20-case Chinese intent evaluation with fail-closed negative cases.
- `ros2 run agent_gateway ask "回充电桩"` for an end-to-end local demo.

It distinguishes:

- Error 10: malformed JSON, empty input, or duplicate fields.
- Error 11: missing fields, extra fields, wrong types, unknown targets, or
  deadline values outside the contract.

The adapter does not contain target coordinates, navigation parameters, retry
logic, behavior trees, or recovery decisions. It does not call Nav2.

## Example

    from agent_gateway import parse_task_request

    request = parse_task_request(
        '{"contract_version":1,"action":"navigate",'
        '"target":"dock","deadline_s":90}'
    )

The normalized request now maps into the outer ExecuteTask Action and is
verified with a fixed FakeModelProvider plus fake navigation server. The next
milestone validates one configured live service without changing the bridge. C++
task_guard remains the final validation authority because another ROS client
could bypass the Gateway.

The provider boundary stays deliberately small:

    user text -> provider -> raw JSON -> parse_task_request -> ExecuteTask

OpenAI, Qwen, or a local model must only replace the provider step. They do not
own coordinates, recovery, cancellation, deadlines, or Nav2 calls.

Run the complete offline proof after building the workspace:

    bash src/embodied-agent-runtime/scripts/smoke_ai_gateway.sh

Run the provider-only intent evaluation without starting ROS nodes:

    ros2 run agent_gateway evaluate_intents --provider fake

The same evaluation can target a configured compatible model. It expects one
task tool call only for a clear, approved target; unsupported, negated, or
multi-target requests must produce no approved task.

For an OpenAI-compatible service, configure the shell without committing a
credential:

    export EMBODIED_AI_PROVIDER=openai-compatible
    export EMBODIED_AI_BASE_URL=<provider-v1-base-url>
    export EMBODIED_AI_MODEL=<provider-model-name>
    export EMBODIED_AI_API_KEY=<secret-if-required>
    ros2 run agent_gateway ask "回充电桩"

Official OpenAI instead uses `OPENAI_MODEL`, `OPENAI_API_KEY`, and the optional
`OPENAI_BASE_URL` (default `https://api.openai.com/v1`). Test either profile
without ROS motion first:

    ros2 run agent_gateway probe_provider --provider openai "回充电桩"
    ros2 run agent_gateway probe_provider --provider openai-compatible "回充电桩"

`config/provider.example.env` documents the variables but intentionally
contains no usable endpoint or secret.
