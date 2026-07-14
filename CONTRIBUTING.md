# Contributing

## Development Environment

Use Ubuntu 24.04 and ROS 2 Jazzy. Place the repository at
`~/embodied_ws/src/embodied-agent-runtime`, deactivate Conda, and use the
system Python required by ROS.

## Change Rules

- Keep model output outside the motion-control boundary.
- Do not add model-generated coordinates, velocity, paths, Behavior Tree XML,
  retry counts, or recovery policy.
- Run Guard validation before any inner navigation Goal can exist.
- Preserve the global monotonic deadline across retries.
- Treat cancellation as confirmed only after the inner Action reaches a
  terminal canceled state within the bounded confirmation window.
- Keep API credentials in local environment variables only.

## Verification

Run the complete local release gate:

```bash
bash scripts/verify_release.sh
```

For a focused Python change, run the relevant `agent_gateway` test first. For
Action lifecycle changes, run the affected launch test before the full gate.

## Pull Requests

Explain the failure scenario being prevented, not only the happy-path feature.
Include commands and output summaries as evidence. Do not claim real Nav2,
TurtleBot3, or live-model validation unless that exact integration was run.
