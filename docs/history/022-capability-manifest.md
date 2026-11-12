# One Capability Manifest Instead of Six Allowlists

The same three lists — workflows, targets, device arguments — lived in six
places with nothing keeping them in step:

| where | what it held |
| --- | --- |
| `runtime_gateway` `allowed()` | two workflow ids, as string comparison |
| `task_orchestrator` `handle_goal` | the same two ids, again |
| `task_executor` `targets` / `target_arguments` | targets and their int32, as separate aligned arrays |
| `task_executor` `device_opcode` | one opcode |
| `ai_task_adapter` `targets`, `workflow_ids` | its own copy |
| `perception_task_adapter` `target_ids`, `workflow_ids` | one per ArUco marker |

Four of those could disagree without anything reporting it. And the innermost
layer — the Device Bridge, which is the last thing before the bus — validated
**nothing**: any opcode and any int32 argument were forwarded, and any other DDS
participant could send them.

## What was added

`runtime_capabilities` reads one YAML file:

- **workflows** — what a caller may ask for;
- **targets** — the names a workflow may name, and the int32 each maps to, so an
  unknown target cannot be smuggled in as a number;
- **device_commands** — opcode plus a *closed* argument set and a per-command
  duration bound. Closed on purpose: a device that rejects most of the int32
  range and one that accepts it look identical until something sends a value
  outside it;
- **limits** — the global duration and step bounds.

The loader rejects a manifest that would silently mean "nothing is allowed":
missing file, no workflows, unknown version, a command bound above the global
limit. Each of those is a configuration error, and a node that loaded it as an
empty set would look like a broken device instead.

## Who reads it

`device_bridge`, `task_executor`, `task_orchestrator` and `runtime_gateway` take
`capabilities_file`, and `runtime_bringup` passes one path to all four. The
bridge now refuses an opcode, argument or timeout that is not declared.

**Empty means "use the built-in lists", not "refuse everything."** Every node
keeps its previous behaviour when the parameter is absent, so an existing
deployment is unaffected. That is a deliberate trade: enforcing the manifest
by default would break every launch line in the README, and refusing to start
would be worse. The gap is that a stack started without the parameter is not
checked — which is why the launch file supplies it, and why the next step is
`GetCapabilities`.

## Measured

`e13` against the real bridge, with a deliberately narrow manifest (one
workflow, one target, opcode 1 with argument 42 only, 5000 ms bound):

| command | result |
| --- | --- |
| opcode 1, argument 42, 2000 ms | accepted, `COMPLETED` |
| opcode 1, argument 0 | **rejected** at the goal response |
| opcode 2, argument 42 | **rejected** |
| opcode 1, argument 42, 6000 ms | **rejected** |
| same stack without `capabilities_file` | accepted — the built-in behaviour is preserved |

The bridge log names the reason:
`Rejecting command_id=202: opcode=1 argument=0 ack_timeout_ms=2000 is not in the
capability manifest`.

`test_manifest` (6 cases) pins the loader: the shipped file loads, the closed
argument set holds, and the four reject cases fail. The shipped file is a
compile-time fixture, so a test cannot pass against a different file than the one
that ships.

## Not done yet

- **No `GetCapabilities` RPC.** The manifest is readable by every node but not by
  a caller, so an AI-side prompt still has to be written by hand from this file.
  That was the next item in the roadmap and is not in this change.
- **The adapters still carry their own parameter lists.** They are inputs to the
  planners, not enforcement points, so they can stay narrower than the manifest
  without creating a hole; making them read the manifest too is cosmetic until
  `GetCapabilities` exists.
- **One opcode per executor.** A manifest declaring several commands is rejected
  by the executor rather than silently picking one; multi-device routing is the
  `multi-device addressing` item, not this one.
- **`workflows.xml` is still fixed.** The manifest doesn't generate behavior
  trees; it declares which of them may be reached.
