# Device Bridge

This package turns a versioned SocketCAN controller heartbeat into a bounded
ROS readiness signal. It does not send velocity, torque, trajectory, or motor
control frames.

Protocol:

```text
CAN interface: configurable, default vcan0
CAN ID:        standard 11-bit 0x321
DLC:           exactly 2
byte 0:        protocol version, exactly 1
byte 1:        bit 0 controller ready; bits 1-7 must be zero
timeout:       configurable, default 500 ms
```

Run with an existing `vcan0` interface:

```bash
ros2 launch device_bridge vcan_device_readiness.launch.py
cansend vcan0 321#0101
ros2 topic echo /device_ready std_msgs/msg/Bool
ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray
```

Any malformed frame, explicit not-ready flag, missing interface, or heartbeat
timeout produces `device_ready=false`. The Runtime only requires this signal
when `require_device_ready:=true` is configured for a hardware deployment.
