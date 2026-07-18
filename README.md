# Robot Runtime

A deterministic ROS 2 task runtime connecting a loopback gRPC workflow boundary to fixed BehaviorTree.CPP orchestration, nested ROS Actions, SocketCAN device control, diagnostics, and SQLite task history.

## Runtime Chain

```text
gRPC -> ExecuteWorkflow/fixed BT -> ExecuteTask -> ExecuteDeviceCommand
-> Device Bridge -> SocketCAN -> device -> TaskEvent -> SQLite
```

The repository contains ten packages: interfaces, CAN protocol, virtual device, Device Bridge, Task Executor, Runtime Monitor, History, Orchestrator, Gateway, and an optional rule-based AI task adapter.

## Candidate Baseline

This candidate targets Ubuntu 24.04 with ROS 2 Jazzy on x86_64 Linux and
generic ARM64 Linux. The ten-package build, tests, and software-only `vcan0`
evidence remain subject to fresh CI and target-host verification.

## Build

```bash
set +u
source /opt/ros/jazzy/setup.bash
set -u
cd ros2_ws
rosdep install --from-paths src --ignore-src --rosdistro jazzy -y
colcon build --cmake-args -DBUILD_TESTING=ON
colcon test --return-code-on-test-failure
colcon test-result --test-result-base build --verbose
```

On x86_64 Linux, build and test directly with the commands above. On ARM64,
start with `scripts/check_arm64_environment.sh`, then run
`scripts/build_on_arm64.sh` and `scripts/run_arm64_smoke.sh`. The default
platform profile is `generic-arm64`; select the optional X5 profile with
`RUNTIME_PLATFORM_PROFILE=x5`. The X5 profile records target intent only and
does not add vendor dependencies or compatibility claims. Never reuse an
x86_64 build, install, or log tree on ARM64.

32-bit ARM is not supported. BPU/NPU runtimes, cameras, GPIO, and physical CAN
adapters remain board-specific integration work behind their respective
adapter boundaries.

## Evidence Boundary

The accepted evidence is a local software chain using SocketCAN `vcan0`. It
does not prove physical CAN hardware, physical stopping, board-specific
BPU/NPU or camera compatibility, GPIO behavior, TLS/authentication, high
availability, or production throughput.
