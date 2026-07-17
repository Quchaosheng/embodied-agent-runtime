# 第十六课：TaskEvent 任务可观测性

## 本课目标

这一课让不持有 Action Goal 的观察者也能看到任务生命周期：

- executor 发布 `task_contract/msg/TaskEvent`。
- 覆盖 VALIDATING、DISPATCHING、RUNNING、RECOVERING、CANCELLING 和全部终止状态。
- 每条终止事件携带稳定 error_code 和 attempt。
- 使用 reliable + transient-local + KeepLast 50 QoS。
- launch test 验证成功、拒绝、取消、deadline、重试、SAFE_STOP 和晚订阅历史。

## 为什么 Action Feedback 不够

Action feedback 只服务当前 Goal 客户端。CLI、网页、Foxglove 或运维节点如果只想观察，
不应该为了获得状态而成为任务客户端。TaskEvent 把低频状态转换发布到独立 Topic：

```text
ExecuteTask lifecycle
  -> /task_events
  -> CLI / Foxglove / recorder / diagnostics bridge / tests
```

距离等高频执行数据仍走 Action feedback；TaskEvent 只在状态转换时发布，避免把事件流变成
另一条重复 telemetry。

## 消息字段

消息契约位于：

```text
task_contract/msg/TaskEvent.msg
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| stamp | ROS 时间戳，用于跨节点时间线展示 |
| task_id | 关联同一个 ExecuteTask Goal |
| state | VALIDATING 到 FAILED/SAFE_STOP 的稳定状态码 |
| error_code | 终止失败原因；正常转换为 0 |
| attempt | 当前或最终导航尝试次数 |
| detail | 面向人的稳定诊断文字 |

task_id 不应该包含姓名、手机号等隐私信息。

## QoS 选择

Publisher 使用：

```text
history: KeepLast 50
reliability: reliable
durability: transient_local
```

### 为什么 reliable

状态转换频率低但价值高，漏掉 RECOVERING 或 SAFE_STOP 会破坏时间线，因此选择可靠传输。

### 为什么 transient-local

Foxglove 或 CLI 可能在任务启动后才连接。只要 executor publisher 仍在运行，晚订阅者可以
立刻收到最近 50 条转换，不必重新执行机器人任务。

### 它不是什么

transient-local 不是数据库，也不是持久审计日志。节点重启后历史消失；50 条是 publisher
内存窗口。需要跨重启回放时应使用 rosbag2 或专门存储。

## 关键事件序列

### 成功

```text
VALIDATING -> DISPATCHING -> RUNNING -> SUCCEEDED
```

### Guard 拒绝

```text
VALIDATING -> FAILED(error 13, attempts 0)
```

### 用户取消

```text
VALIDATING -> DISPATCHING -> RUNNING
  -> CANCELLING -> CANCELLED
```

### Deadline

```text
VALIDATING -> DISPATCHING -> RUNNING
  -> CANCELLING -> FAILED(error 32)
```

### 重试成功

```text
VALIDATING -> DISPATCHING(1) -> RUNNING(1)
  -> RECOVERING(1) -> DISPATCHING(2) -> RUNNING(2)
  -> SUCCEEDED(2)
```

### 恢复耗尽

```text
... -> RECOVERING(1) -> DISPATCHING(2) -> RUNNING(2)
  -> SAFE_STOP(error 34, attempts 2)
```

## 如何观察

构建并 source 后：

```bash
ros2 topic echo /task_events task_contract/msg/TaskEvent \
  --qos-reliability reliable \
  --qos-durability transient_local \
  --qos-depth 50
```

如果 executor 在 namespace 中，Topic 也跟随 namespace，例如：

```text
/retry_success/task_events
```

## 时间戳与 Deadline 为什么用不同的时钟

TaskEvent stamp 使用 ROS clock，便于仿真时间和跨节点可视化。任务 deadline 仍使用
`steady_clock`，避免系统时间调整导致超时预算跳变。一个用于展示，一个用于安全计时。

## 测试设计

现有 launch tests 不只检查“收到过事件”，而是锁定完整顺序：

- 成功为四个状态。
- 非法目标在 VALIDATING 后直接 FAILED，error 13、attempt 0。
- 取消必须经过 CANCELLING。
- deadline 必须经过 CANCELLING 并以 error 32 结束。
- retry 的第二次 DISPATCHING/RUNNING 使用 attempt 2。
- 两次导航失败以 SAFE_STOP + error 34 结束。
- 新订阅者在任务完成后创建，仍能收到完整保留历史。

当前完整结果：

```text
Summary: 65 tests, 0 errors, 0 failures, 0 skipped
```

## TaskEvent 与 Diagnostics 的区别

| TaskEvent | ROS diagnostics |
| --- | --- |
| 描述某个任务经历了什么 | 描述组件当前是否健康 |
| 以 task_id 组织时间线 | 以节点/硬件名称组织状态 |
| 状态转换时发布 | 周期发布或健康变化时发布 |
| 已实现 | 下一阶段 |

例如“任务进入 SAFE_STOP”是 TaskEvent；“定位不可用”是 diagnostics。两者应该关联，但
不能混成同一种消息。

## 技术复习问题与答案

### 问：为什么不把所有 Nav2 feedback 都发成 TaskEvent？

答：TaskEvent 是状态转换流，Nav2 feedback 是连续进度。复制每帧距离会增加噪声和带宽，
也让观察者难以识别真正的状态变化。当前只在 RUNNING 入口发布一次事件，距离继续走
Action feedback。

### 问：reliable 会不会阻塞机器人执行？

答：事件频率很低，publisher 与 Action 执行解耦；可靠 QoS 用于减少关键转换丢失。生产
部署仍应监控慢订阅者和 DDS 资源限制，不能把 Topic 当硬实时安全通道。

### 问：晚订阅历史为什么只保留 50 条？

答：它足够观察最近若干任务，同时给内存设置明确上限。需要长期历史时使用 rosbag 或
数据库，而不是无限增大 DDS history。

### 问：TaskEvent 能证明机器人真的停下了吗？

答：不能单独证明。CANCELLED 事件来自 executor 已确认内层 Action 进入 CANCELED；最终
物理停止仍依赖 Nav2、底盘和硬件安全链路。事件是软件证据，不是硬件急停反馈。

## 本课完成状态

- [x] TaskEvent publisher。
- [x] reliable + transient-local + KeepLast 50。
- [x] 所有终止路径携带 error code 和 attempt。
- [x] 成功、拒绝、取消、deadline、retry、SAFE_STOP 序列测试。
- [x] 晚订阅者历史测试。
- [x] 当前总计 65 tests 通过。
- [ ] ROS diagnostics 健康状态。
- [x] rosbag2/MCAP 持久回读（见第二十课）。
- [ ] Foxglove 面板。
