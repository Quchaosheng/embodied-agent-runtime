# 第五课：用 Smoke Test 证明双层 Action

## 本课目标

这一课不是再堆一个功能，而是学习如何把代码变成技术复习官可以复现的证据：

- 启动两个真实 ROS 2 Action Server 进程。
- 验证外层 ExecuteTask 的 Goal、Feedback、Result 生命周期。
- 验证非法目标在内层 NavigateToPose Goal 产生前被 Guard 拦截。
- 明确 smoke test、单元测试和 `launch_testing` 的边界。

## 一条命令复现

在工作区执行：

    cd ~/embodied_ws
    source /opt/ros/jazzy/setup.bash
    source install/setup.bash
    bash src/embodied-agent-runtime/scripts/smoke_phase_2.sh

脚本会启动：

    /navigate_to_pose  <- fake_navigate_to_pose_server
    /execute_task      <- execute_task_server

退出时会自动停止两个后台进程。失败时临时日志会保留在 `/tmp` 并打印路径。

## 观察到的成功路径

请求：

    {contract_version: 1, action: 1, task_id: smoke-dock, target: dock, deadline_s: 30}

执行链：

1. CLI 发送外层 `ExecuteTask` Goal。
2. `execute_task_server` 调用 `task_guard` 验证契约和机器人就绪状态。
3. `dock` 被映射为受 Runtime 控制的 placeholder `map` Pose。
4. Runtime 创建内层 `NavigateToPose` Goal。
5. 假导航服务器发送距离 `3.0`、`2.0`、`1.0` 三次 feedback。
6. Runtime 把内层反馈转换为外层 `state=3`、`attempt=1` 的反馈。
7. 内层成功被映射为外层 `final_state=5`、`error_code=0`。

技术复习表达：

> 这个 smoke test 不是只检查函数返回值，而是启动真实 ROS 2 Action 通信，
> 证明外层业务协议和内层导航协议之间的 feedback/result 转换已经连通。

## 观察到的拒绝路径

请求目标改为 `laboratory` 后：

    final_state: 9
    error_code: 13
    attempts: 0

当前 `handle_goal` 对所有传输层 Goal 都先返回 `ACCEPT_AND_EXECUTE`，所以 CLI
会先显示 `Goal accepted`。随后执行线程中的 Guard 返回 `kUnknownTarget`，在
调用 `async_send_goal` 之前结束外层任务。`attempts=0` 是关键证据：没有导航
尝试，也就没有运动副作用。

这解释了技术复习中容易混淆的一点：

- ROS Action 层 accepted：通信对象已建立。
- 业务 Guard accepted：任务满足安全策略，可以创建内层导航 Goal。

当前实现只保证第二个条件未满足时不发送内层 Goal。后续 code review 可以讨论
是否把静态契约检查提前到 `handle_goal`，以及如何把动态 RobotContext 检查留在
执行线程。

## 三种测试的边界

| 测试 | 能证明什么 | 当前状态 |
| --- | --- | --- |
| Guard/Gateway 单元测试 | 纯函数和边界输入 | 21 个通过 |
| M2 smoke script | 多进程 ROS Action wiring、成功/feedback/拒绝 | 已通过 |
| `launch_testing` | success、feedback、拒绝、cancel、deadline、进程退出 | 5 个用例通过 |

Smoke test 单独不能证明取消确认竞态；现在这部分由 `launch_testing` 覆盖。
两者仍不能证明失败 retry 或真实 Nav2 规划器行为。

## 技术复习问题与答案

### 问：为什么假服务器还使用真实 `nav2_msgs`？

答：因为客户端代码不应该为测试切换协议。假服务器只替代导航行为，保留真实
ROS Action 类型和通信栈；将来接入 Nav2 时，Runtime 的内层客户端不需要改写。

### 问：为什么未知目标还显示 Goal accepted？

答：当前实现先接受 ROS 传输层 Goal，再在线程中执行 Guard。Guard 仍然在内层
Goal 创建前运行，因此安全保证没有被破坏；但这个交互细节值得在 code review
中讨论是否需要更早拒绝。

### 问：这是不是已经证明可以控制真实机器人？

答：不是。它证明 Runtime 的双层 Action 生命周期和安全拦截逻辑连通。真实
Nav2、地图、定位、控制器和 TurtleBot3 仿真仍属于后续里程碑。

### 问：下一步为什么不是直接接仿真？

答：成功、反馈、拒绝和取消已经由 `launch_testing` 固化；下一步增加 deadline
和有限 recovery。这样真实 Nav2 出问题时，可以区分通信层、Runtime 和导航栈
的故障来源。

## 本课完成状态

- [x] `scripts/smoke_phase_2.sh` 可重复执行。
- [x] dock 成功和三次 feedback 已验证。
- [x] laboratory 在内层 Goal 前被拒绝已验证。
- [x] `launch_testing` 自动化 Goal/Feedback/Cancel 测试。
- [x] 全局 deadline 到期后确认取消内层 Goal。
- [ ] retry 不重置原始 deadline。
