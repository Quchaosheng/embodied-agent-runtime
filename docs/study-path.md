# Ordered Study Path

Do not try to learn every item at once. Complete each stage only after its
small proof works. The first seven stages are the core project; SocketCAN is a
separate extension that makes the system more realistic after the core is
reliable.

| Stage | Learn | Build immediately | Evidence |
| --- | --- | --- | --- |
| 0 | C++, CMake, `package.xml`, `colcon`, and Git | Build `task_contract` and `task_guard` | Guard unit tests pass |
| 1 | ROS 2 Nodes, Topics, Services, parameters, and Launch | A small status publisher and configuration loader | Parameters appear in logs and launch works |
| 2 | ROS 2 Actions | Outer `ExecuteTask` Action plus fake navigation Action | Success, feedback, rejection, and cancel launch tests pass |
| 3 | Timeout, cancellation, custom interfaces, and YAML policy | Global deadline and named-target allowlist | Timeout cancels navigation; invalid request never reaches it |
| 4 | Lifecycle, readiness, diagnostics, and safe stop | Explicit node readiness and fault states | Runtime rejects tasks while not ready |
| 5 | `launch_testing` and `rosbag2` | Deterministic fault tests and one recorded failure | Failure can be replayed and explained |
| 6 | Nav2 and BehaviorTree.CPP | Real `NavigateToPose` adapter and reviewed recovery flow | Fixed retry and SAFE_STOP pass against fake Nav2; real Nav2 remains |
| 7 | AI JSON contract | Minimal Gateway adapter with fixed-schema output | Swapping model provider changes no C++ safety code |
| 8 | `vcan0`, `can-utils`, SocketCAN filtering, and heartbeat timeout | Read-only device-ready bridge | Missing, invalid, or stale heartbeat blocks task execution with error 18 |

## Why SocketCAN is last

TurtleBot3/Nav2 navigation does not need a CAN bridge for the first safety
runtime. Adding SocketCAN before Action cancellation, task policy, and tests
would make failures hard to attribute. The implemented `vcan0` extension is
valuable because it drives the same Linux `PF_CAN` receive path as a physical
CAN interface and gates `RobotContext` readiness.

## Scope of the device bridge

The implemented first bridge:

1. Receive a heartbeat or state frame from `vcan0`.
2. Publish device readiness and diagnostics.
3. Reject wrong ID, DLC, protocol version, reserved bits, and non-data frames.
4. Mark the device unavailable after heartbeat timeout.
5. Cause `task_guard` to reject new tasks with error 18 before Nav2 is called.

It does not command TurtleBot3 velocity or claim hardware safety. A bounded
request/ACK command protocol belongs to a later real-device milestone, after
the actual controller command set and safe-stop behavior are known.
