# Architecture

## Trust boundary

```text
User intent or model output
  -> agent_gateway
  -> ExecuteTask Action Server
  -> task_guard
  -> task_executor
  -> Nav2 NavigateToPose Action Client
  -> robot or TurtleBot3 simulation

Controller heartbeat
  -> Linux PF_CAN raw socket
  -> device_bridge
  -> /device_ready
  -> RobotContext
```

`agent_gateway` is untrusted with respect to robot motion. It only normalizes
model output into the JSON contract. The C++ `task_guard` validates the parsed
task again, checks runtime state, and enforces the static policy.

## Runtime ownership

The runtime owns:

- Named-target to pose mapping.
- Allowed targets, maximum deadline, retry count, and keepout policy.
- The task state machine and all cancellation propagation.
- Navigation result handling and recovery decisions.
- A single atomic task reservation released on every terminal path.

`RobotContext` is assembled at request time. Localization readiness requires a
live `map -> base_link` TF, navigation readiness requires the Nav2
`NavigateToPose` Action server, and task activity comes from the atomic
reservation. Hardware deployments may additionally require a fresh
`/device_ready` heartbeat; simulation leaves that check disabled. The same
values are published on `/diagnostics`; they are no longer hard-coded test
booleans.

Nav2 owns planning, control, local obstacle avoidance, and its internal
recovery behavior. The model owns none of these decisions.

## Optional device-readiness extension

The implemented `device_bridge` consumes a strict versioned heartbeat from
`vcan0` or a physical SocketCAN interface. It publishes `/device_ready=false`
when the interface is missing, the frame is malformed or not-ready, or the
heartbeat exceeds its timeout. `task_executor` applies a second monotonic
freshness timeout so a bridge crash cannot leave a latched `true` value.

The bridge is deliberately read-only: it sends no velocity, torque,
trajectory, enable, or emergency-stop frame. It proves a communication-health
boundary and error-18 task gate, not physical actuator control or certified
hardware safety.

## Task state machine

```text
IDLE -> VALIDATING -> DISPATCHING -> RUNNING -> SUCCEEDED
RUNNING -> CANCELLING -> SAFE_STOP -> CANCELLED
RUNNING -> RECOVERING -> RUNNING
RECOVERING -> SAFE_STOP -> FAILED
```

The task deadline is global. A retry never resets the original deadline. A
cancel request propagates from the outer `ExecuteTask` Action to the inner
Nav2 Action and is bounded by a cancel-confirmation timeout.

`SAFE_STOP` is a software state. Hardware emergency-stop and motor watchdog
requirements remain the responsibility of the robot platform.
