# 第四课：ROS 2 Action 生命周期

## 本课目标

理解 ROS 2 Action 的四个核心概念：

- Goal：客户端请求执行一个长任务。
- Feedback：服务器执行期间持续报告进度。
- Result：任务最终成功、失败或取消的结果。
- Cancel：客户端请求停止一个尚未结束的 Goal。

## 为什么导航使用 Action

导航不是立即完成的请求。它可能运行几十秒，需要显示剩余距离，也必须允许用户取消。

Service 适合短时间请求响应：

    request -> response

Action 适合长任务：

    Goal -> accepted -> Feedback... -> Result
                         |
                         -> Cancel

技术复习标准答案：

问题：为什么不用 Service 调导航？

答案：Service 没有原生的持续反馈和 Goal 取消语义。Action 把任务身份、反馈、取消和最终结果组合成完整生命周期，更适合导航。

## Action Server 的三个回调

rclcpp_action Action Server 通常注册三个回调。

### handle_goal

收到 Goal 后立即决定：

- REJECT
- ACCEPT_AND_EXECUTE
- ACCEPT_AND_DEFER

这个回调必须快速返回，不能在里面执行长任务。

### handle_cancel

客户端请求取消时决定是否接受取消。

接受取消不等于任务已经停止。服务器还需要让执行线程停止，并最终调用 canceled 返回结果。

### handle_accepted

Goal 被接受后启动真正的异步执行。

常见实现会启动线程或把任务交给执行队列，避免阻塞 ROS executor。

## 为什么不能在 handle_goal 中执行任务

handle_goal 属于通信回调。如果在里面循环导航或 sleep，ROS executor 可能无法及时处理：

- 取消请求。
- 新消息。
- 定时器。
- 其他回调。

正确方式是快速校验并返回，然后在 handle_accepted 之后异步执行。

## 本项目的双层 Action

本项目有两层 Action：

    agent_gateway
      -> ExecuteTask
      -> task_executor
      -> NavigateToPose
      -> fake server or Nav2

### 外层 ExecuteTask

面向业务任务：

- 命名目标。
- 总 deadline。
- 任务状态。
- 业务错误码。
- 尝试次数。

### 内层 NavigateToPose

面向导航执行：

- 目标 Pose。
- 导航反馈。
- Nav2 结果。
- 内部 Goal 取消。

技术复习标准答案：

问题：为什么不直接让 Gateway 调 NavigateToPose？

答案：Gateway 不应该知道坐标或 Nav2 细节。外层 ExecuteTask 保持稳定的业务契约，Runtime 在内部把命名目标映射为 Pose，并负责安全检查、总超时和取消传播。

## 取消传播

外层 ExecuteTask 被取消时，Runtime 必须：

1. 接受或拒绝外层取消请求。
2. 找到对应的内部 NavigateToPose Goal。
3. 请求取消内部 Goal。
4. 等待有上限的取消确认。
5. 内部确认后返回外层 CANCELLED。
6. 确认超时则进入 SAFE_STOP 或明确故障。

不能在收到外层取消后立即声称成功取消，因为机器人可能仍在执行内部 Goal。

## Feedback 转换

内层 Nav2 feedback 包含导航信息，例如 distance_remaining。

外层 Runtime 不应把所有 Nav2 内部字段直接暴露，而是转换为稳定的 ExecuteTask feedback：

- state
- attempt
- distance_remaining
- detail

这样未来替换导航实现时，模型和上层客户端不需要跟着修改。

## Result 转换

内层结果需要映射为外层业务结果：

- Nav2 succeeded -> STATE_SUCCEEDED
- 客户端取消且确认 -> STATE_CANCELLED
- Nav2 rejected -> kNavRejected
- Nav2 aborted -> kNavAborted
- 总 deadline 到期 -> kTaskTimedOut
- 取消未确认 -> kCancelUnconfirmed
- 恢复耗尽 -> kRecoveryExhausted

## 为什么先做假导航服务器

真实 Nav2 依赖地图、定位、规划器、控制器和仿真时钟，故障来源很多。

假 NavigateToPose Server 可以确定性模拟：

- 接受并成功。
- 拒绝 Goal。
- 连续发送 feedback。
- Abort。
- 延迟执行。
- 接受或拒绝取消。

这样可以先证明 Runtime 的 Action 生命周期正确，再接入真实 Nav2。

## 假服务器为什么使用真实 nav2_msgs 接口

假服务器与真实 Nav2 都使用 nav2_msgs/action/NavigateToPose。

好处：

- task_executor 不需要为测试切换 Action 类型。
- 从假服务器切换 Nav2 时只更换服务端。
- 测试覆盖的客户端代码就是生产客户端代码。

本项目已经安装 `nav2_msgs`，因此假服务器和真实 Nav2 客户端使用同一套
Action 类型：

    sudo apt install -y ros-jazzy-nav2-msgs

上面的安装命令只需要在依赖缺失时执行一次。

## 当前 M2 证据

从工作区执行：

    bash src/embodied-agent-runtime/scripts/smoke_phase_2.sh

当前已经观察到：

1. `dock` 被接受，产生 3 次外层 feedback，最终 `final_state: 5`。
2. `laboratory` 的 ROS Action Goal 在通信层被接受，但在执行线程中被
   `task_guard` 语义拒绝，最终 `error_code: 13`、`attempts: 0`。
3. 因为 Guard 在 `async_send_goal` 之前执行，第二条路径不会创建内层
   `NavigateToPose` Goal。

这里要区分两种“接受”：当前 `handle_goal` 先接受 ROS Action 传输层的
Goal，业务 Guard 再决定是否执行；因此非法任务显示 `Goal accepted` 后仍
会以 `ABORTED` 结果结束。这是当前实现的真实行为，也是后续可以优化为
早期拒绝的 code review 点。

## M2 自动化测试证据

当前 `launch_testing` 自动测试已经覆盖：

1. 外层 Goal 被接受。
2. 内层服务器产生三次 feedback。
3. 内层成功映射为外层成功。
4. 无效任务在内层 Goal 产生前被拒绝。
5. 外层取消传播到内层，并等待内层进入 `CANCELED` 终态。
6. deadline 到期取消内层 Goal，并返回 `kTaskTimedOut`。
7. 两个被测节点在测试结束后正常退出。

## 高频技术复习问题

问题：接受 cancel 是否代表机器人已经停下？

答案：不是。它只代表服务器愿意处理取消。Runtime 还必须等待内部导航 Goal 的终止确认，并设置最大等待时间。

问题：为什么外层和内层要分别有 Goal Handle？

答案：它们属于不同协议和不同生命周期。Runtime 需要保存两者的映射，才能把 feedback、取消和结果正确地关联到同一个业务任务。

问题：为什么 Action Server 回调要快速返回？

答案：阻塞回调会妨碍 executor 处理取消、反馈和其他通信。长任务应该异步执行。

问题：假服务器是不是 mock？

答案：它是进程内或测试进程中的确定性 Action Server，使用真实 ROS 2 Action 通信和 nav2_msgs 类型。它替代的是导航行为，不替代 rclcpp_action 通信栈。

## 下一步

- 增加 Nav2 失败的确定性场景。
- 实现不重置原始 deadline 的有限 retry/recovery。
