# 第十八课：实机 Readiness、单任务占用与 ROS Diagnostics

## 这一课修复了什么

`task_guard` 从第一课开始就定义了三个实时输入：

```text
localization_ready + navigation_ready + task_active
```

但此前 `execute_task_server` 调用 Guard 时传入的是固定
`{true, true, false}`。这适合先验证 Action 生命周期，却不能用于真实机器人：定位已经
丢失时仍会被当成 ready，同时两个 Goal 可能由两个线程一起发给 Nav2。

本课把三个布尔值接到真实运行状态：

| RobotContext 字段 | 当前来源 | 失败结果 |
| --- | --- | --- |
| `localization_ready` | TF 是否存在 `map -> base_link` | error 16 |
| `navigation_ready` | Nav2 `NavigateToPose` Action 是否被发现 | error 17 |
| `task_active` | 原子任务槽是否已被占用 | error 15 |

## 为什么用 TF 判断定位

Nav2 的定位链路最终必须提供 `map -> odom -> base_link`。Runtime 不需要知道 AMCL、
SLAM Toolbox 或其他定位算法的内部状态，只需要确认当前 TF 树能够把机器人位姿转换到
地图坐标。这样仿真和实机使用同一个接口，也允许未来替换定位实现。

仅检查 `/amcl_pose` 是否存在不够，因为消息可能已经停止更新；仅检查 AMCL 进程存在
也不代表 TF 可用。当前第一版使用“最新 transform 可查询”作为明确的最小 readiness
条件，后续实机阶段还应增加 transform age、定位协方差和传感器心跳上限。

## 为什么用 Action discovery 判断 Nav2

`NavigateToPose` 是 Runtime 真正依赖的执行接口。planner 或 controller 单个节点存在，
不代表整个 lifecycle stack 已经可接收任务。`action_server_is_ready()` 至少证明当前 ROS
Graph 中存在兼容的 Action Server；真实 Nav2 smoke 仍会额外等待 `bt_navigator`
lifecycle 进入 active。

## 原子 BUSY 如何避免两个任务同时执行

每个执行线程创建 `TaskReservation`，内部使用
`std::atomic<bool>::compare_exchange_strong`：

```text
false -> true 成功：当前 Goal 获得唯一任务槽
已经是 true：当前 Goal 返回 kTaskAlreadyRunning / error 15
```

任务槽采用 RAII。成功、Guard 拒绝、Nav2 拒绝、取消、deadline、恢复耗尽或异常返回
都会离开作用域，由析构函数统一把状态恢复为 false。这比在十几个 `return` 前手动清理
更不容易漏掉失败路径。

## 为什么 Action Goal 先 accepted，再返回 BUSY

ROS Action 的 `GoalResponse::REJECT` 只能表达通信层“不接受”，无法携带本项目定义的
error 15 和解释文本。当前设计先接受通信 Goal，再由 Guard 返回业务失败结果：

```text
Goal accepted by ROS transport
  -> Guard checks live RobotContext
  -> FAILED + error 15 + "a task is already active"
```

这让 Gateway、日志和测试都能区分未知目标、定位未就绪、Nav2 未就绪和机器人忙碌。

## `/diagnostics` 提供什么

执行器每秒向标准 `/diagnostics` 发布：

- `localization_ready`
- `navigation_ready`
- `task_active`
- `required_transform=map -> base_link`
- `OK/WARN/ERROR` 和可读原因

查看命令：

```bash
source ~/embodied_ws/install/setup.bash
ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray
```

定位或 Nav2 缺失时为 ERROR。Fake smoke 因为没有 TF，可以显式设置
`localization_check_enabled:=false`，此时 diagnostics 为 WARN，避免把测试旁路误认为
生产 ready。

## 新增测试证据

新增和扩展的 launch tests 覆盖：

1. 第一条任务运行时，第二条任务返回 error 15、attempts 0。
2. 静态 `map -> base_link` TF 与 Fake Nav2 都存在时任务成功。
3. TF 缺失时返回 error 16，而且不发送内部 Goal。
4. Nav2 Action 缺失时返回 error 17，而且不进入导航等待循环。
5. `/diagnostics` 的布尔键值和 ERROR/OK 等级与上述状态一致。
6. 所有测试进程干净退出，任务槽不会污染下一条测试。

## 对未来实机意味着什么

接 TurtleBot3 或自研差速底盘时，Runtime 不需要知道电机驱动细节。实机系统必须提供：

```text
/tf、/tf_static       map -> odom -> base_link
/navigate_to_pose     Nav2 Action Server
/scan、/odom          Nav2 定位和规划所需输入
/cmd_vel              底盘速度接口
```

之后再把电池、急停、碰撞条、CAN 心跳和电机 ACK 聚合成更多 readiness 输入。软件
`SAFE_STOP` 和 diagnostics 不能替代物理急停与电机 watchdog。

## 技术复习官常问

### 1. 为什么不用普通 bool？

Action Server 会为每个 accepted Goal 启动执行线程。普通 bool 的读取和写入会产生数据
竞争，也不能保证“检查为 false”和“改为 true”是一个不可分割操作。CAS 同时完成检查
和占用，只有一个线程成功。

### 2. RAII 在这里解决的具体问题是什么？

执行函数有大量提前返回：Guard 拒绝、Action 不可用、取消、deadline、成功和
SAFE_STOP。RAII 把任务槽释放绑定到对象生命周期，新增返回路径时无需记住额外清理。

### 3. TF 存在是否就能证明定位绝对可靠？

不能。它证明最基本的坐标链可用，不证明误差足够小。实机版还需要检查 TF 年龄、
定位协方差、激光数据时间戳和 localization lifecycle；本课没有夸大这一点。

### 4. diagnostics 能阻止机器人运动吗？

不能。diagnostics 是状态输出；真正阻止任务的是同一组 `RobotContext` 输入进入 Guard。
这样监控看到的原因和安全决策使用的事实来自同一计算路径。

### 5. 为什么 Fake 测试允许关闭定位检查？

Fake Server 的目标是确定性制造 Action 成功、取消和恢复故障，它不模拟完整 TF 树。
旁路必须显式参数开启，并通过 WARN 暴露。仿真和实机默认值始终是检查定位。
