# ResetFault: The Declared Way Back From a Latched Error

`robot_task_interfaces/srv/ResetFault.srv` has existed since the interfaces were
first written and had no server anywhere. Meanwhile both components latched
`ERROR` on any failure, `/runtime/ready` went false, and the only way back was
restarting processes — which, per 017 and the stop-id fix, is itself the
expensive operation against a device that keeps its replay cache.

The service now has two implementations, one per component that latches.

## What the services do

`~/reset_fault` on `device_bridge` and on `task_executor`. Both take the
declared request and both refuse work they cannot do safely:

| condition | result |
| --- | --- |
| a command or task is in flight | refused, `accepted=false` |
| `clear_latched_fault=false` with an error latched | refused, says a fault is latched |
| `clear_latched_fault=false` with nothing latched | refused, says nothing is latched |
| `device_id` set on the executor | refused: it belongs to the bridge |
| `device_id` set on the bridge but not its own | refused, names the expected id |
| `clear_latched_fault=true`, idle | cleared, `accepted=true` |

`clear_latched_fault=false` is therefore a **probe**, not a failed reset: a
supervising caller reads whether something is latched before deciding to reset
it. A plain "clear" would have been ambiguous.

The executor's `device_id` is empty by default and the bridge's is the
`device_id` parameter (default `virtual_ecu_1`), which until now existed only in
the virtual device. Sending the wrong id to the wrong node is a caller bug and
is reported rather than ignored.

## What a reset does and does not clear

Cleared: the latch that forces `ready=false`, in one component.

Kept, deliberately:

- **counters** — `ack_timeout_count`, `device_fault_count`, `stop_failure_count`,
  `retry_count` are the evidence of what happened;
- **`last_outcome` / `last_error_code`** — erasing them would make a reset look
  like a task that never failed.

A reset is not a claim that the fault is gone. It is a statement that an
operator has seen it. The next failure latches again.

Recovery still takes two calls when both components failed, because each latches
independently. That is the honest shape: the bridge is not allowed to clear the
executor's latch, and the executor cannot clear the device's.

## Verified

`e11` on the real nodes (AF_UNIX transport, not SocketCAN):

| step | result |
| --- | --- |
| ready before | true |
| device politely refuses one command | `DEVICE_FAULT/301`, ready **false** |
| executor probe | `accepted=false`, "a failure is latched; set clear_latched_fault to clear it" |
| executor clear | `accepted=true`, "latched failure cleared" |
| ready after executor clear | **false** — the bridge is still latched, as intended |
| bridge probe | `accepted=false`, "a fault is latched..." |
| bridge clear | `accepted=true`, "latched fault cleared" |
| bridge clear with a foreign `device_id` | `accepted=false`, names the expected id |
| ready after both | **true** |
| a new task | `COMPLETED` |
| ready at end | true |

Nothing was restarted between the refusal and the successful task. Before this
change that sequence required killing and restarting two processes.

Unit tests: `test_diagnostic_state` in both packages gained a reset case and a
"nothing was latched" case; both check that counters and the last outcome
survive the clear. 6/6 in `device_bridge`, 11/11 in `task_executor` (that count
includes the lint targets).

## What this does not do

- **The latch policy is unchanged.** A polite device refusal still latches. The
  earlier review suggested not latching on a refusal; that is a policy question
  with a safety side (a refusal can mean the device rejected a command it could
  not execute) and is left as it was.
- **No automatic reset.** Nothing calls these services yet. An operator or a
  supervisor has to, and the launch file's `respawn` stays off for the reason
  given in 019.
- **No `device_id` on the executor's own identity.** The executor refuses any
  non-empty `device_id` rather than pretending to have one.
