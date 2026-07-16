# 第十九课：SocketCAN 设备心跳与失败关闭

## 这一课完成了什么

第十八课的 `RobotContext` 已经使用真实 TF、Nav2 Action discovery
和原子任务槽，但还没有物理设备通信输入。本课新增
`device_bridge`，把 Linux SocketCAN 心跳转成 Runtime 可选的
`device_ready` 门禁。

完整数据流是：

```text
CAN controller / vcan0
  -> Linux PF_CAN raw socket
  -> socketcan_heartbeat_node
  -> HeartbeatMonitor
  -> /device_ready + /diagnostics
  -> task_executor freshness check
  -> RobotContext.device_ready
  -> task_guard
  -> error 18 or continue to Nav2
```

关键安全属性是：没有新鲜的合法心跳时，新任务不会产生
Nav2 Goal。

## 为什么用 `vcan0`

`vcan0` 是 Linux 内核的虚拟 CAN 网卡。它没有真实收发器，但与
`can0` 共用 SocketCAN API。因此本项目验证的不是一个伪造的 ROS
Topic，而是真实的：

- `PF_CAN` raw socket 创建与 bind。
- CAN interface 索引查询。
- 内核 CAN ID 过滤。
- 非阻塞帧读取。
- 标准帧、RTR、error frame 和扩展帧的区分。

将来接物理 CAN 时，主要变化是网卡名和底层网卡配置，
心跳解析器和 Runtime 门禁可以保持不变。

## 两字节心跳协议

```text
CAN ID:   标准 11-bit，默认 0x321
DLC:      必须等于 2
byte 0:   协议版本，必须等于 1
byte 1:   bit 0 = ready，bit 1-7 必须等于 0
timeout:  默认 500 ms
```

`cansend vcan0 321#0101` 的含义：

```text
321   -> CAN ID 0x321
#     -> ID 与 payload 分隔符
01    -> version 1
01    -> ready=true，保留位全部为 0
```

`321#0100` 是合法的 not-ready 心跳，会立即使
`device_ready=false`。错误版本、错误 DLC 或保留位非零则是非法帧，
不会刷新心跳时间。

## 为什么是两层超时

### 第一层：`device_bridge`

`HeartbeatMonitor` 使用 `std::chrono::steady_clock`。最后一帧合法 ready
心跳超过 500 ms 后，bridge 发布 `false`。这层检测控制器停止
发帧。

### 第二层：`task_executor`

Runtime 不只保存 Bool，还记录最后一次 `/device_ready` 到达的单调
时间。即使 bridge 最后发布的是 `true`，随后进程崩溃，Runtime
也会在默认 1000 ms 后将状态判为 stale。

这两层解决不同故障：

```text
controller stops heartbeat -> bridge timeout
bridge stops publishing     -> Runtime timeout
```

## 为什么仿真默认不要求 CAN

`task_executor` 参数 `require_device_ready` 默认为 `false`。因为
TurtleBot3 仿真和 Fake Nav2 没有这个自定义 CAN 控制器，若默认强制开启，
所有仿真都会被无意义地阻断。

实机 launch 应显式开启：

```bash
ros2 run task_executor execute_task_server --ros-args \
  -p require_device_ready:=true \
  -p device_ready_timeout_ms:=1000
```

这是部署策略选择，不是模型可以修改的任务字段。

## 代码阅读顺序

1. `device_bridge/include/device_bridge/heartbeat_monitor.hpp`
2. `device_bridge/src/heartbeat_monitor.cpp`
3. `device_bridge/test/test_heartbeat_monitor.cpp`
4. `device_bridge/src/socketcan_heartbeat_node.cpp`
5. `task_guard/src/task_guard.cpp`
6. `task_executor/src/execute_task_server.cpp`
7. `task_executor/test/test_runtime_readiness_launch.py`
8. `scripts/smoke_vcan_readiness.sh`

先看纯 C++ 解析器，再看 Linux socket 节点，最后追踪布尔值如何进入
Guard。这样不会一开始就被 ROS 和 SocketCAN 两套 API 绕晕。

## 在本机复现

安装和创建虚拟 CAN：

```bash
sudo apt install can-utils
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set vcan0 up
ip -details link show vcan0
```

构建和测试：

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-up-to task_executor device_bridge
source install/setup.bash
colcon test --packages-select task_guard task_executor device_bridge
colcon test-result --verbose
```

从仓库根目录运行真实 `PF_CAN` smoke：

```bash
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws \
  bash scripts/smoke_vcan_readiness.sh
```

它自动验证：

1. 没有心跳：`error_code=18`、`attempts=0`。
2. 每 100 ms 发送合法心跳：任务成功。
3. 停止心跳并等待 stale：再次 `error_code=18`、`attempts=0`。

## 为什么这不是底盘驱动

`device_bridge` 只读心跳。它不发送：

- 速度或转向指令。
- 扭矩、轨迹或电机使能。
- 急停或恢复命令。

因此技术复习时应说“完成 SocketCAN 通信 readiness 集成”，不应说
“完成真实电机控制”。实机阶段还需要物理急停、电机 watchdog、
速度限制、故障复位流程和底盘协议审核。

## 技术复习官常问

### 1. 为什么非法帧不刷新 heartbeat timestamp？

否则一个持续发错误版本或错误长度的故障设备，会因为“一直有流量”
而被误判为健康。只有符合当前协议的帧才能证明设备状态有效。

### 2. not-ready 帧为什么反而是 accepted frame？

它的格式和版本都合法，只是控制器明确声明当前不能执行。解析器应接受
这个状态并立即失败关闭，而不是将它当作格式错误。

### 3. 为什么使用 `steady_clock`？

心跳超时测量的是经过时长。系统时间可能被 NTP 或人工调整向前或向后
跳变，单调时钟不会，更适合 timeout 判定。

### 4. 为什么 Runtime 还要自己做 stale 检查？

它防御 bridge 本身崩溃。安全判断不能只依赖一个最后状态值，还要求
状态在有限时间内持续到达。

### 5. 如何证明拒绝发生在 Nav2 Goal 之前？

launch test 和 vcan smoke 都断言 `error_code=18` 且 `attempts=0`。
`attempts` 只在准备发送内层导航 Goal 时增加，因此 0 是没有进入 Nav2
执行流程的可观测证据。

## 这一课的一句话总结

我用真实 Linux SocketCAN 接收路径将版本化控制器心跳接入
`RobotContext`，并用 bridge 与 Runtime 两层单调超时保证缺失、
not-ready 或 stale 输入在 Nav2 Goal 产生前返回 error 18。
