# STOP ACK 不能冒充业务 ACK：独立 command-id 命名空间的排障记录

## 现象

我排查取消后的偶发 ACK 异常时，最危险的假设是“只要收到一条格式正确的响应，就可以结束当前等待”。STOP 也是一条 CAN 命令，也会产生 ACK；如果 STOP 与业务命令复用 command-id，或者发送 STOP 后遗留的响应进入下一次业务等待窗口，旧 STOP ACK 就可能被当成新业务 ACK。这样系统表面上会更快完成，实际上却把“设备确认停止”解释成了“设备确认执行业务”。

排障时我把“STOP 已发送、正在等待或清理其响应”的阶段记作 `safe_stop_sent`。这是本文使用的阶段标签，不是当前源码中的变量名。进入该阶段后，STOP ACK 只能服务于这次 STOP；一旦它迟到到后续业务窗口，就必须按协议作为不匹配帧忽略，不能污染业务 ACK。

## 排查证据

协议边界首先写在 `ros2_ws/src/runtime_can/include/runtime_can/protocol.hpp`：普通业务 command-id 最大为 `0x7FFF`，STOP command-id 从 `0x8000` 开始，STOP opcode 为 `0xFF`。`ros2_ws/src/runtime_can/test/test_protocol.cpp` 的 `ReservesSeparateStopCommandNamespace` 明确断言两个区间相邻但不重叠。

分配端也遵守这个边界。`ros2_ws/src/task_executor/src/task_executor_node.cpp` 的 `allocate_command_id()` 在超过 `kMaxApplicationCommandId = 0x7FFF` 后回绕到 `1`；`ros2_ws/src/device_bridge/src/device_bridge_node.cpp` 的 `allocate_stop_command_id()` 从 `kStopCommandIdMin` 分配并在 `uint16_t` 上限后回绕到 `0x8000`。因此业务命令不会合法地产生 `0x8000+` 的 ID。

我用以下命令核对常量、分配器和接收过滤：

```bash
rg -n "kApplicationCommandIdMax|kStopCommandIdMin|kStopOpcode" ros2_ws/src/runtime_can
rg -n "allocate_command_id|allocate_stop_command_id" \
  ros2_ws/src/task_executor/src ros2_ws/src/device_bridge/src
rg -n "Ignoring unmatched response|expected_command_id" \
  ros2_ws/src/device_bridge/src/device_bridge_node.cpp
```

最关键的运行时证据在 `receive_response()`：解码成功还不够，只有 `decoded->command_id == expected_command_id` 才会返回 `kResponse`；否则记录 `Ignoring unmatched response` 并继续等待。业务 ACK 等待传入原业务 ID，STOP ACK 等待传入新分配的 STOP ID，两者不会串线。

## 根因/设计

根因不是 CAN 帧缺少类型，而是相关性设计不足。协议帧里的 opcode 能区分发送的命令，却不能替代响应与请求之间的唯一关联；响应结构依赖 command-id 归属。如果 STOP 复用原业务 ID，迟到业务 ACK 与 STOP ACK 将共享同一关联键，接收端无法可靠判断“这条响应确认了什么”。

因此我采用双重隔离。第一层是命名空间：`1..0x7FFF` 只给业务，`0x8000..0xFFFF` 只给 STOP。第二层是等待上下文：每次 `receive_response()` 都绑定唯一 `expected_command_id`。在 `safe_stop_sent` 阶段，匹配当前 STOP ID 且结果为成功、设备模式为 `STOPPED` 的响应可以完成 STOP；不匹配的帧继续忽略。若 STOP ACK 在该窗口结束后才到达，后续业务等待看到的是 `0x8000+`，仍会因 ID 不匹配被丢开，而不会完成业务 Action。

## 修复与回归

协议单测先锁住静态边界：

```bash
colcon test --packages-select runtime_can device_bridge
colcon test-result --verbose
```

E2E 再验证线上的帧关联。`scripts/run_industrial_e2e.sh` 通过 `candump -L vcan0` 记录 `0x100` 命令帧和 `0x101` 响应帧；`assert_stop_can` 从 opcode `0xFF` 的命令中提取 STOP ID，再要求响应的 command-id 与它一致。`cancel` 场景要求恰有一条 STOP 响应且设备模式为 `STOPPED`；`drop_stop_ack` 场景要求没有该 STOP 响应，并最终得到 `SAFE_STOP/204`。`ack_timeout` 场景还证明业务 ACK 超时后发送 STOP，最终仍保留业务故障 `201`，同时确认 STOP 已被设备应答。

回归审查时我还会专门看日志中的 `STOP sent stop_command_id=... original_command_id=...` 和 `Ignoring unmatched response ... expected=...`。前者证明 STOP 没有复用业务 ID，后者证明迟到帧不会抢占当前等待。对于“迟到 STOP ACK 穿越到下一业务窗口”的精确时序，当前协议代码给出了过滤保证，但仓库中没有一条以该名称单独注入迟到帧的专用测试；这是证据范围，而不是可以用现有场景替代的事实。

## 证据边界

上述结论能够证明 command-id 空间不重叠、软件接收端按 expected ID 过滤，以及 vcan 场景中的 STOP 帧和 Action 结果一致。它不能证明真实 CAN 控制器不会重排、重复或长期缓存帧，也不能证明设备固件实现了同样的去重和关联规则；这些需要实机抓包和固件侧日志共同确认。

同样，STOP ACK 只表示协议上收到了声明为 `STOPPED` 的响应。`vcan0` 和软件证据不等于硬件急停，不证明执行器失能、制动器闭合或安全回路动作。涉及人身和设备安全时，我仍把硬件急停验证作为独立验收项。
