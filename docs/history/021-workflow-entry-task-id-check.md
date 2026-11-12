# The Workflow Entry Point Refuses a Reused task_id

`ExecuteWorkflow` is the boundary the README draws: "models and perception
adapters cannot directly control a device; they submit allowlisted workflows."
One of the four entry paths did not go through it.

| entry | used to enter at | allowlist | ready precondition | reused-`task_id` check |
| --- | --- | --- | --- | --- |
| gRPC gateway | `ExecuteWorkflow` | yes | yes | yes |
| model adapter | `ExecuteWorkflow` | yes | yes | **no** |
| ArUco adapter | `ExecuteWorkflow` | yes | yes | **no** |
| rule adapter (C++) | **`ExecuteTask`** | **no** | **no** | **no** |

So the C++ rule adapter skipped the workflow allowlist and the ready
precondition entirely, and none of the three ROS-side adapters was stopped from
reusing a `task_id`.

That last one is not cosmetic: `task_id` is the `task_runs` primary key. A second
run under an identity that is already recorded has its terminal `TaskEvent`
dropped by `INSERT OR IGNORE`, so the history keeps the older run, `GetTask`
answers with the older run, and the recorder counts a write error that turns
`runtime/history` diagnostics into `ERROR` for the rest of the process's life —
because the counter never resets.

## What changed

**The check moved to where the paths converge.** `ExecuteWorkflow` is reached by
all four entries, so it is where a reused `task_id` is refused:

- empty `task_id` → rejected (`EMPTY_TASK_ID`);
- `task_id` with a recorded terminal row → rejected
  (`TASK_ID_ALREADY_RECORDED`);
- the history is unreadable → rejected, not accepted. Accepting on a failed
  check would be the silent re-run this exists to prevent.

`database_path` on the orchestrator selects this. **Empty means "do not check"**,
and the node logs a warning saying so at startup rather than opening a file and
pretending. That keeps every existing invocation that starts an orchestrator
without a database working, while making the gap visible instead of silent.

The lookup runs on a read-only connection (`022`), so a wrong path fails at
startup instead of creating an empty database in which every identity looks
unused.

**The rule adapter joins the same entry point.** `ai_task_adapter_node` now
submits `ExecuteWorkflow` instead of `ExecuteTask`. It gained `workflow_id`
(default `single_task`) and `request_id` (default: the planned `task_id`, so two
invocations with one identity are one request). Feedback and result logging moved
from `state`/`progress` to `active_node`/`progress`.

## Measured

`e12` runs the deployed adapter binary against the real stack — device, bridge,
executor, monitor, history, orchestrator — with the AF_UNIX transport:

| step | result |
| --- | --- |
| adapter, `task_id=rule-entry-1` | rc 0, accepted, `ExecuteWorkflow result ... outcome=0`, feedback seen through `ExecuteTask` |
| adapter, same `task_id` again | rc 2, `ExecuteWorkflow goal rejected`; orchestrator logs `TASK_ID_ALREADY_RECORDED` |
| adapter, `task_id=rule-entry-2` | rc 0, accepted, completes |
| `task_runs` | exactly `rule-entry-1`, `rule-entry-2` |
| ready at end | true |

The adapter is never the thing that refuses: it gets a rejected goal response
from the workflow entry point, which is the single place the decision lives.

`test_entry_validation` covers the entry point directly (3 cases): a recorded id
is refused while an unused one still runs; an empty id is refused; and an
orchestrator without `database_path` still runs, pinning that the check is
skipped-and-warned rather than silently passed.

## Not changed, and why

- **The model and ArUco adapters are untouched.** They already enter at
  `ExecuteWorkflow`, so the new check protects them without either changing. The
  model adapter's fixed default `task_id=model_task_1` remains: a second
  invocation with it is now refused at the entry point instead of corrupting the
  history, which is the honest outcome for a caller that did not supply an
  identity.
- **No gRPC hop was added.** Per the decision on this task, the check lives in
  the ROS entry point rather than in the gateway, so a ROS adapter does not link
  gRPC and the Python model adapter does not need `grpcio`. The gateway keeps its
  own check for its own callers; that is now a second line of defence, not the
  only one.
- **`task_orchestrator` needs `runtime_history`.** It is a new package
  dependency, which CONTRIBUTING asks to be declared — it is, in `package.xml`
  and `CMakeLists.txt`.
- **The monitor README's scenarios now start the orchestrator.** They launched
  only bridge and executor, which the rule adapter could reach directly; through
  `ExecuteWorkflow` that scenario would fail with "server not available".
