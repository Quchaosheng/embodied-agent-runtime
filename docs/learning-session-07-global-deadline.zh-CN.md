# 第七课：全局 Deadline 与超时取消

## 本课目标

这一课实现任务级的总时间预算：

- deadline 从任务进入 executor 时开始计算。
- 等待导航服务、发送内层 Goal、执行导航都消耗同一份预算。
- deadline 到期后取消内层 Goal。
- retry 以后只能使用剩余时间，不能重新获得完整预算。

## Timeout 和 Deadline 的区别

相对 timeout 表示“从当前阶段开始再等多久”。如果每次重试都重新设置 timeout，
两次 30 秒尝试可能让总任务运行 60 秒以上。

绝对 deadline 表示“这个任务最晚何时必须停止正常执行”：

    started_at = steady_clock::now()
    task_deadline = started_at + deadline_s

后续所有阶段只读取 `task_deadline`，不重新计算它。

技术复习标准答案：

> deadline 是任务级总预算，不是单次导航尝试的 timeout。retry 可以消耗剩余预算，
> 但不能重置开始时间，否则有限次数的 retry 仍可能造成不可控的总时长。

## 为什么使用 steady_clock

`steady_clock` 是单调时钟，不会因为系统时间校准、NTP 或用户修改时间而倒退。
任务内部的耗时和截止判断应该使用单调时钟；ROS 时间仍用于消息时间戳和仿真时间。

## 当前执行路径

    ExecuteTask accepted
      -> record started_at
      -> Guard validation
      -> wait for NavigateToPose server
      -> send inner Goal
      -> wait feedback/result
      -> task_deadline reached
      -> request inner cancel
      -> wait inner result == CANCELED
      -> outer FAILED + kTaskTimedOut

等待导航服务和等待 Goal response 仍有各自的两秒技术上限，但真正等待时间取“两秒
上限”和“任务剩余时间”中的较小值。因此内部阶段不能越过任务 deadline。

## 为什么 deadline 后还有 500 ms

deadline 到期表示不能继续正常导航，不表示可以瞬间确认机器人已经停止。Runtime
仍必须发送 cancel 并等待内层 Action 进入 `CANCELED`。

当前规则是：

    normal work deadline = task_deadline
    cancel confirmation deadline = task_deadline + 500 ms

这 500 ms 是固定的安全确认宽限，不是新的导航预算。成功结果不能利用它继续执行。

结果映射：

- 期限到期且取消确认：`kTaskTimedOut = 32`。
- 期限到期但取消未确认：`kCancelUnconfirmed = 33`。

区分这两个错误很重要：32 表示任务超时但停止已确认；33 表示 Runtime 不能证明
内层导航已经停止，需要进入更高等级的故障处理。

## 自动化测试如何制造超时

假导航服务器新增 `feedback_delay_ms` 参数。生产默认值仍是 50 ms；launch test
把它设为 400 ms：

- success 用例 deadline 为 3 秒，约 1.3 秒成功。
- timeout 用例 deadline 为 1 秒，在导航结束前触发取消。
- timeout 最终断言 `error_code=32`、`attempts=1`。

这比在测试里依赖机器负载更可靠，因为慢速行为由版本控制的参数明确决定。

## 当前证据

执行：

    bash scripts/build_phase_2.sh

结果：

    Summary: 4 packages finished
    Summary: 32 tests, 0 errors, 0 failures, 0 skipped

executor 的 xUnit 明确包含 5 个 launch 用例，其中：

    test_deadline_cancels_navigation ... passed

## 技术复习问题与答案

### 问：为什么不直接在 deadline 到期时返回失败？

答：返回失败只改变上层状态，不能停止内部导航。必须先取消内层 Goal，并等待有
上限的终态确认，才能说明任务执行已经结束。

### 问：500 ms 宽限是不是违反 deadline？

答：它不是执行预算，而是停止确认预算。deadline 后不再允许导航成功或 retry；
宽限只用于证明内部 Goal 已停止，并且上限固定为原始 deadline 加 500 ms。

### 问：以后加入 retry 时如何保证不重置时间？

答：把 `task_deadline` 保存在整个外层 Goal 生命周期中。每次尝试前检查剩余时间，
所有 wait 都使用同一个绝对时间点，不再根据 `deadline_s` 创建新 timeout。

### 问：超时后为什么返回 FAILED，不返回 CANCELLED？

答：取消是 Runtime 为执行 deadline 策略采取的内部动作，任务本身没有按期完成，
所以外层业务结果是 FAILED，并携带 `kTaskTimedOut`。用户主动取消才返回 CANCELLED。

## 本课完成状态

- [x] 单调时钟全局 deadline。
- [x] 导航服务、Goal response 和执行阶段共享预算。
- [x] 超时后确认内层取消。
- [x] timeout launch test。
- [x] bounded retry 使用同一个原始 deadline。
- [x] recovery exhausted 和 SAFE_STOP 映射。
