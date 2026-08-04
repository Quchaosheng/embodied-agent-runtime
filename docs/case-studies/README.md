# Engineering Case Studies

These notes record concrete failure symptoms, evidence, design decisions, and
regression boundaries from the runtime implementation.

- [父子层取消预算不能打平：一次 113 覆盖 204 的排障记录](parent-child-timeout-budget.md)
- [STOP ACK 不能冒充业务 ACK：独立 command-id 命名空间的排障记录](stop-ack-command-namespace.md)

The current evidence is software, ROS 2 Action, and SocketCAN/vcan evidence
unless a note explicitly says otherwise. It is not actuator or hardware
emergency-stop evidence.
