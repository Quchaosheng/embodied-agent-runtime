# 第八课：有限重试、RECOVERING 与 SAFE_STOP

## 本课目标

这一课完成 M3 的最小恢复闭环：

- 恢复上限来自版本控制的 YAML 策略。
- 第一次导航失败后只允许再尝试一次。
- retry 继续使用原始任务 deadline。
- 两次失败后进入 `SAFE_STOP`，返回 `kRecoveryExhausted=34`。

## 策略从哪里来

执行器启动时加载安装后的：

    task_guard/config/task_policy.yaml

关键字段：

    recovery:
      max_navigation_attempts: 2
      cancel_confirmation_timeout_ms: 500

这里的 2 表示总尝试次数，不是“第一次之外再重试两次”。因此最多执行：

    attempt 1 -> failed -> RECOVERING
    attempt 2 -> succeeded or SAFE_STOP

模型不能修改这两个值，也不能通过请求字段指定 retry 次数。

## 为什么把取消字段改为毫秒

旧配置使用 `cancel_confirmation_timeout_s: 5`，但执行器的安全设计实际需要 500 ms。
如果配置单位只能是整数秒，YAML 和代码就会出现两个不同真相。

现在字段改为 `cancel_confirmation_timeout_ms: 500`，并由 executor 真正读取。配置、
代码和技术复习说明终于使用同一个来源。

## 恢复状态流

    attempt 1 RUNNING
      -> SUCCEEDED                     结束成功
      -> ABORTED
           -> RECOVERING feedback
           -> attempt 2 RUNNING
                -> SUCCEEDED           attempts=2
                -> ABORTED             SAFE_STOP + error 34

`RECOVERING` feedback 中的 attempt 表示刚刚失败的尝试。下一轮 Nav2 feedback 会带
新的 attempt 编号，因此上层可以清楚看到状态变化。

## 哪些失败会重试

当前最小实现只对已经被内部 Action Server 接受、但最终没有成功的导航结果进行
有限重试。

以下错误不会盲目 retry：

- 外层契约或 Guard 拒绝。
- NavigateToPose server 不存在。
- 内层 Goal 被拒绝。
- 用户主动取消。
- 全局 deadline 到期。

这些错误重试通常不会自动变好，继续发送 Goal 反而可能制造更多副作用。

## Retry 为什么没有重置 Deadline

`task_deadline` 在进入 executor 时只计算一次，并位于 attempt 循环外部。第二次
尝试仍比较同一个绝对时间点。

专门的测试让第一次失败消耗大部分一秒预算：

    attempt 1 fails near deadline
      -> attempt 2 starts
      -> original deadline expires
      -> inner Goal canceled
      -> FAILED + kTaskTimedOut

如果代码错误地为 attempt 2 新建一秒 timeout，这个测试会成功而不是超时。

## 为什么最终是 SAFE_STOP

`SAFE_STOP` 是 Runtime 的软件状态，表示：

- 已经停止继续创建导航尝试。
- 固定恢复预算已耗尽。
- 上层必须人工处理或进入更高层故障流程。

它不等于硬件急停，也不能替代电机 watchdog、急停按钮或底盘安全控制器。

## 为什么暂时不用 BehaviorTree.CPP

当前恢复行为只有“失败后再尝试一次”，一个显式 C++ for 循环最容易审查和测试。
现在引入 BehaviorTree.CPP 只会增加依赖和调试表面。

当项目加入明确的恢复动作，例如清除局部代价地图、后退固定距离、重新定位时，
再把这些受审查动作组织成静态 Behavior Tree。模型仍不能生成树或修改次数。

## 自动化测试

恢复测试使用三个隔离 ROS namespace，避免假服务器的失败计数互相污染：

| Namespace | 假服务器行为 | 预期结果 |
| --- | --- | --- |
| `/retry_success` | 第一个内层 Goal abort | 第二次成功，attempts 2 |
| `/recovery_exhausted` | 前两个 Goal abort | SAFE_STOP，error 34 |
| `/retry_deadline` | 首次 abort 且执行较慢 | 第二次受原 deadline 限制，error 32 |

executor 的恢复 xUnit 明确包含 4 个用例，包括进程退出检查。

## 当前证据

执行：

    bash scripts/build_phase_2.sh

结果：

    Summary: 4 packages finished
    Summary: 32 tests, 0 errors, 0 failures, 0 skipped

## 技术复习问题与答案

### 问：为什么不是无限 retry，反正导航可能下一次成功？

答：无限 retry 会让任务总时长和机器人行为不可预测。恢复次数必须来自静态策略，
并同时受全局 deadline 限制，才能给出最坏情况上界。

### 问：为什么 error 31 和 error 34 都存在？

答：`kNavAborted=31` 表示单次导航失败；`kRecoveryExhausted=34` 表示 Runtime 已经
用完允许的全部恢复尝试。上层更关心最终任务级结论，因此耗尽后返回 34。

### 问：为什么第二次失败进入 SAFE_STOP，不直接再发一次？

答：YAML 明确规定总尝试次数为 2。超过该值意味着绕过安全策略，可能形成无限
运动循环。Runtime 必须停止创建新 Goal，并给出明确终态。

### 问：为什么 recovery 策略不能由大模型决定？

答：retry 次数和取消上限直接影响运动持续时间与风险，属于安全策略。模型只负责
选择命名任务，恢复预算必须由版本控制配置和确定性 C++ 执行。

## 本课完成状态

- [x] executor 加载已安装 YAML 策略。
- [x] 最多两次导航尝试。
- [x] RECOVERING feedback 和 attempt 编号。
- [x] recovery exhausted 映射为 SAFE_STOP + error 34。
- [x] retry 不重置原始 deadline 的自动测试。
- [x] 版本控制的 target pose 加载。
- [ ] 真实 Nav2/TurtleBot3 集成。
