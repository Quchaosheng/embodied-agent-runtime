# Device Completion States

`COMPLETED` used to mean the device protocol acknowledged the command. That is
still what it means by default, and the README states the boundary explicitly —
a matching CAN ACK is a protocol response, not proof of physical motion. This
adds the next functional step for devices that expose more than an ACK.

## What a protocol can now report

A status frame (`0x102`) is separate from the command ACK (`0x101`), so a
device that only acknowledges is never confused with one that reports execution:

| protocol state | meaning |
| --- | --- |
| `accepted` | the device took the command, movement not started |
| `executing` | movement in progress |
| `completed` | the device says the command reached its end state |
| `faulted` | the device reports a fault code |

None of these are physical-motion evidence. They are protocol states, and the
README boundary still applies to all of them.

## The two modes

`completion_states_enabled` (default **false**) decides which one a deployment
uses:

- **false — ACK-only.** The behaviour is unchanged: an ACK settles the command,
  and the result carries `completion = COMPLETION_ACK_ONLY` so a reader can see
  that "COMPLETED" here means "acknowledged", not "finished". This is the
  default because waiting for a status frame that a device never sends would
  turn every command into a 30 s timeout.
- **true — completion reported.** After the ACK the bridge waits for status
  frames until `completed`, `faulted`, or `completion_timeout_ms` (default
  30000). `accepted` and `executing` are published as feedback and the wait
  continues.

An unrecognised completion byte is **rejected** at decode, not read as
`completed`: guessing there would report motion the device never confirmed.

## The timeout decision

"Executing" without a bound is not a state machine, so a command that never
reports a terminal state is settled as a **reported gap**, not a completion:
`SAFE_STOP/206` with a device STOP attempt, the same shape the existing
`ack_timeout` path uses. A fault settles as `DEVICE_FAULT` by its own state
before the timeout can fire — the mapping is in `classify_status()` and that
ordering has a test.

## What is tested, and against what

`virtual_can_device` gained three modes that separate the ACK from completion:

| mode | behaviour |
| --- | --- |
| `completion` | ACK, accepted, executing, then completed |
| `completion_fault` | ACK, accepted, then faulted |
| `completion_never_finishes` | ACK, accepted, executing, then nothing |

Test coverage:

- `test_protocol` (8 cases) — the status frame is disjoint from the ACK, every
  completion state round-trips, an unknown state and a wrong frame id are
  rejected.
- `test_command_policy` (7 cases) — `classify_status()` maps each state, the
  ACK-only path is labelled rather than assumed, missing status on a
  completion protocol is a timeout, and a fault is settled by its own state.

**Not covered:** the E2E was not extended with a `completion` scenario, and the
bridge's wait loop itself was not exercised against `vcan0` on this host — it
has no CAN interface (`/proc/net/can` does not exist and the module needs root).
The decision logic and the wire format are unit-tested; the loop that drives
them is not, and that gap is stated rather than glossed over.

## What this still does not prove

A device reporting `completed` is a device protocol statement. It is not a
measured stopping distance, not a verified actuator position, and not evidence
that the physical system did what the protocol says. No part of this change
changes the README's evidence boundary.
