# 第二十课：rosbag2 MCAP 任务持久审计

## 本课目标

第十六课的 `/task_events` 已经能让 Foxglove、CLI 或测试观察任务
状态，但 transient-local 历史只存在于 executor 进程内存中。本课用
rosbag2 将 TaskEvent 存入 MCAP，再从磁盘读回并验证精确时间线。

证据链是：

```text
ExecuteTask lifecycle
  -> /task_events
  -> ros2 bag record
  -> MCAP file
  -> rosbag2_py SequentialReader
  -> exact state/error/attempt assertions
```

## 为什么 transient-local 不够

TaskEvent publisher 使用 `reliable + transient_local + KeepLast 50`。它解决的是：

- 任务已经开始后，晚连接的订阅者仍能看到最近状态。
- 内存中最多保留 50 条转换，资源上限明确。

它不能解决：

- executor 重启后历史保留。
- 另一台机器上的离线分析。
- 可重复的故障回放。

rosbag2 订阅 Topic 并将序列化消息写入文件，因此它与
transient-local 是两个层次：一个提供运行时观察，一个提供跨进程
持久证据。

## 为什么选 MCAP

MCAP 是 rosbag2 的现成存储插件，支持 Topic 类型、时间戳和索引。
本项目不自己解析 MCAP 二进制字节，而是使用：

```text
rosbag2_py.SequentialReader
rclpy.serialization.deserialize_message
rosidl_runtime_py.utilities.get_message
```

这样存储格式细节由上游库维护，本项目只负责 TaskEvent 语义。

## 录制的两条任务

### 成功任务

```text
task_id=bag-success
target=dock

VALIDATING -> DISPATCHING -> RUNNING -> SUCCEEDED
```

终止事件必须是：

```text
state=5 error_code=0 attempt=1
```

### Guard 拒绝

```text
task_id=bag-rejected
target=laboratory

VALIDATING -> FAILED
```

终止事件必须是：

```text
state=9 error_code=13 attempt=0
```

`attempt=0` 很重要：它证明未知目标在内层 Nav2 Goal 产生前就被拒绝。

## 读取器做什么

`scripts/audit_task_event_bag.py` 只做三件事：

1. 用 rosbag2 API 打开指定 MCAP bag。
2. 只反序列化 `/task_events`。
3. 每条事件输出一行稳定文本。

例如：

```text
task_id=bag-success state=5 error_code=0 attempt=1 detail=navigation succeeded
```

它不决定哪些状态是合法的。状态契约仍由 `TaskEvent.msg` 和 Runtime
测试维护；smoke 脚本对两条固定任务做精确断言。

## 如何复现

从仓库根目录执行：

```bash
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag \
  bash scripts/smoke_task_event_bag.sh
```

脚本会自动：

1. 启动 Fake NavigateToPose 和 executor。
2. 等待两个 Action Server 被 ROS Graph 发现。
3. 启动 MCAP recorder 并等待它订阅 `/task_events`。
4. 发送成功和拒绝任务。
5. 用 SIGTERM 让后台 recorder 正常 flush 并退出。
6. 从磁盘读回并断言时间线。

成功后临时 bag 会被删除。失败时保留路径和进程日志，便于查因。

## 为什么不把 bag 提交到 Git

- bag 是可重新生成的运行产物。
- 长时间录制可以快速变成大文件。
- 真机 bag 可能包含地图、位置、图像、声音或用户输入。

仓库保存“如何生成和验证证据”的脚本，而不是保存每次生成的证据文件。
`.gitignore` 只排除 `*.mcap` 和本项目的录包目录模式，没有粗暴忽略
所有 `metadata.yaml`。

## 这个 bag 能证明什么

能证明：

- Runtime 已发布预期 TaskEvent 序列。
- 事件被 rosbag2 写入磁盘后可读。
- 错误码和尝试次数与 Action 结果一致。

不能单独证明：

- 真实机器人已经物理停止。
- Nav2 的规划路径一定安全。
- 传感器、底盘或电机 watchdog 正常。

TaskEvent 是软件事件证据，不是物理安全证明。

## 技术复习官常问

### 1. TaskEvent 和 rosbag2 各解决什么？

TaskEvent 定义“任务发生了什么”的稳定消息契约；rosbag2 将这些消息
持久化并支持回放。一个是语义，一个是存储与运输工具。

### 2. 为什么不直接解析 MCAP 字节？

直接解析会把项目绑定到存储细节，还要自己处理索引、序列化和消息
类型。使用 rosbag2 公开 API 代码更少，也能跟随 ROS 2 Jazzy 的类型支持。

### 3. 如何知道 recorder 真的开始记录了？

脚本不使用固定 sleep 猜测，而是查看 `/task_events` 的 subscription count，
等到至少一个订阅者后才发送任务，而且等待有明确上限。

### 4. 为什么用 SIGTERM 结束后台 recorder？

在非交互 Bash 中，后台进程可能继承忽略 SIGINT 的行为。实际验证中
SIGINT 不会让后台 recorder 退出，SIGTERM 则触发 rosbag2 正常 flush 并结束。

### 5. rosbag2 是不是完整审计系统？

不是。它提供消息录制和回放，但默认没有组织级访问控制、签名、不可
篡改保存、脱敏和留存策略。本课的“审计”是可重复的工程证据，不是合规审计。

## 本课证据

```text
65 colcon tests: 0 errors, 0 failures
2 bag-reader tests: OK
20/20 offline intent evaluation
Runtime smoke: PASS
AI Gateway smoke: PASS
TaskEvent MCAP audit smoke: PASS
```

## 一句话总结

我用标准 rosbag2/MCAP 将低频 TaskEvent 跨进程持久化，再通过公开
rosbag2 Python API 读回成功和 Guard 拒绝时间线，对状态顺序、错误码和
attempt 做确定性断言，而不引入自定义存储层。
