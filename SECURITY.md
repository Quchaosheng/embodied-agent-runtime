# Security Policy

## Scope

This repository is a learning and project project. It demonstrates a
software safety boundary for model-requested ROS 2 tasks; it is not a certified
functional-safety system, hardware emergency stop, or production robot safety
controller.

## Reporting

Do not open a public issue containing API keys, tokens, private URLs, robot
credentials, maps from a private site, or exploitable deployment details. Use
the repository owner's private GitHub contact channel after the repository is
published.

## Credential Handling

- Keep OpenAI or relay keys in environment variables.
- Never commit `.env`, `*.pem`, `*.key`, GitHub PATs, or SSH private keys.
- Use a relay-specific revocable key; never give an official provider key to an
  untrusted relay.
- Rotate a credential immediately if it appears in Git history or logs.

## Runtime Boundary

All model and relay responses are untrusted. JSON Schema and the C++ Guard
reduce the executable surface, but they cannot prove that a provider mapped
natural language to the correct allowed target. Production use still requires
a trusted provider, deployment-specific policy, hardware safety controls, and
system-level validation.
