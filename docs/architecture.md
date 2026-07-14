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

Nav2 owns planning, control, local obstacle avoidance, and its internal
recovery behavior. The model owns none of these decisions.

## Optional device-readiness extension

After the core Runtime is stable, an optional `device_bridge` may consume
SocketCAN frames from `vcan0` or a physical CAN interface. It publishes a
device-ready signal that becomes one input to `RobotContext`. The bridge is a
health and acknowledgement integration boundary; it must not claim to control
the TurtleBot3 base unless the physical platform actually uses that CAN bus.

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
