# 第一课：TaskRequest、GuardPolicy、RobotContext

## 本课目标

学完这一课，需要能解释四件事：

1. 为什么模型请求不能直接变成机器人运动。
2. TaskRequest、GuardPolicy、RobotContext 分别负责什么。
3. 为什么 JSON Schema 和 allowed_targets 要同时存在。
4. Task Guard 的每个测试到底证明了什么。

## 一句话模型

    TaskRequest + GuardPolicy + RobotContext -> ValidationResult

只有请求、策略和机器人状态全部通过，Runtime 才能进入后续导航阶段。

## 用现实例子理解

可以把机器人任务看成“员工申请驾驶公司车辆”。

- TaskRequest 是员工提交的申请：想去哪里、做什么、多久完成。
- GuardPolicy 是公司的固定规定：哪些地点允许去、最大时限是多少。
- RobotContext 是车辆的实时状态：定位是否正常、导航系统是否正常、车辆是否正在执行别的任务。
- ValidationResult 是审批结果：允许执行，或者给出明确拒绝原因。

申请内容正确，不代表现在一定能执行。规则允许，也不代表车辆当前可用。

## 三种数据的区别

| 类型 | 回答的问题 | 数据性质 | 当前代码示例 |
| --- | --- | --- | --- |
| TaskRequest | 调用方想做什么？ | 每次任务都会变化 | action、target、deadline_s |
| GuardPolicy | 当前部署允许什么？ | 静态、受审查、版本控制 | contract_version、期限范围、目标白名单 |
| RobotContext | 机器人现在能不能做？ | 实时变化 | localization_ready、navigation_ready、task_active |

## TaskRequest

TaskRequest 位于 task_contract/include/task_contract/types.hpp。

一个合法请求可以表达：

    contract_version = 1
    action = navigate
    target = dock
    deadline_s = 90

它只描述任务意图，不包含实际坐标、速度、轨迹、行为树或重试次数。

技术复习标准答案：

问题：为什么 TaskRequest 不包含坐标？

答案：坐标是安全关键数据，而且依赖具体地图版本。模型只选择命名目标，真实 Pose 保存在受版本控制的 Runtime 配置中，才能进行审查、测试和回归。

## GuardPolicy

GuardPolicy 位于 task_guard/include/task_guard/task_guard.hpp。

当前字段包括：

- contract_version：Runtime 支持的协议版本。
- min_deadline_s 和 max_deadline_s：任务期限边界。
- allowed_targets：当前部署允许的目标子集。

对应配置位于 task_guard/config/task_policy.yaml。当前 C++ 默认值与 YAML 一致，但 YAML 加载器尚未实现，这是下一阶段任务。

技术复习标准答案：

问题：JSON Schema 已经限定了目标，为什么 GuardPolicy 还要 allowed_targets？

答案：Schema 定义整个协议认识哪些目标，GuardPolicy 定义当前部署实际允许哪些目标。Schema 是能力上限，策略是部署子集。策略可以进一步收紧能力，但不能扩大协议。

例子：

- Schema 认识 dock、home、workbench。
- 某现场维护期间，Policy 只允许 home。
- navigate dock 在语法上合法，但策略上必须拒绝。

## RobotContext

RobotContext 位于 task_guard/include/task_guard/task_guard.hpp。

当前字段包括：

- localization_ready：定位是否可靠。
- navigation_ready：导航系统是否可用。
- task_active：是否已有任务执行。

技术复习标准答案：

问题：请求合法、策略也允许，为什么还可能拒绝？

答案：安全执行还依赖机器人实时状态。定位未就绪时机器人不知道自己在哪里；导航未就绪时无法可靠规划；已有任务时继续接收新任务可能产生资源冲突。因此动态上下文必须独立检查。

## Guard 验证顺序

task_guard/src/task_guard.cpp 当前按照以下顺序失败关闭：

1. contract_version
2. action
3. allowed_targets
4. deadline
5. task_active
6. localization_ready
7. navigation_ready
8. 全部通过后返回 kOk

这里使用 fail-fast：发现第一个明确问题就立即返回对应错误码。

为什么先检查请求和策略，再检查机器人状态？

因为无效请求不需要访问后续执行资源。先完成纯数据检查，也能让错误原因更稳定、更容易测试。

## 一个完整例子

请求：

    navigate dock, deadline 90

策略：

    允许 dock
    deadline 范围 1 到 90

上下文：

    localization_ready = true
    navigation_ready = true
    task_active = false

结果：

    kOk，允许进入后续执行阶段

如果只把 localization_ready 改成 false：

    返回 kRobotNotReady
    不允许产生 Nav2 Goal

如果只把策略白名单改成 home：

    返回 kUnknownTarget
    即使 Schema 认识 dock，也不允许当前部署执行

## 当前测试分别证明什么

| 测试 | 证明的规则 |
| --- | --- |
| AcceptsAllowedTaskForReadyRobot | 请求、策略和上下文全部合法时接受 |
| RejectsUnsupportedContractVersion | 不猜测未知协议语义 |
| RejectsUnknownTarget | 契约外目标被拒绝 |
| RejectsContractTargetExcludedByPolicy | 策略可以收紧契约 |
| RejectsDeadlineOutsidePolicy | 总期限必须在安全范围内 |
| RejectsTaskWhenRobotIsBusy | 同一时间不接受冲突任务 |
| RejectsTaskWhenLocalizationIsUnavailable | 没有可靠定位时禁止导航 |
| RejectsTaskWhenNavigationIsUnavailable | 导航系统未就绪时禁止任务 |

colcon 当前汇总结果为：

    9 tests, 0 errors, 0 failures, 0 skipped

汇总数字包含测试框架的测试项。真正重要的证据是 0 errors 和 0 failures。

## 当前测试还不能证明什么

这些测试只覆盖 Task Guard 的纯决策逻辑，还不能证明：

- ROS 2 Action 的 Goal、Feedback、Result 和 Cancel。
- 外层任务取消是否传递给 Nav2。
- 全局 deadline 是否在重试后保持不变。
- TurtleBot3 是否能到达命名目标。
- 模型 Gateway 是否正确解析 JSON。

技术复习时要主动说明边界，不能把后续规划说成已经实现。

## 高频追问

问题：为什么每种拒绝原因要用不同错误码？

答案：上层需要区分协议错误、策略拒绝和机器人未就绪。明确错误码方便日志、监控、测试和故障定位，也避免上层根据字符串猜测原因。

问题：Task Guard 是不是硬件安全系统？

答案：不是。它是任务级软件安全边界。硬件急停、电机 watchdog 和底盘安全仍由机器人平台负责。

问题：为什么 Guard 适合写成纯 C++ 类？

答案：纯决策逻辑不依赖 ROS 通信，单元测试运行快且确定。ROS 节点只需要收集请求和上下文，再调用 Guard。这样业务规则与通信层解耦。

## 技术复习回答模板

回答设计题时使用这个结构：

1. 先说风险：模型输出不确定，机器人运动有物理副作用。
2. 再说边界：模型表达意图，Runtime 决策，Nav2 执行。
3. 再说机制：Contract、Policy、Context 三层检查。
4. 最后说证据：当前 9 项测试全部通过。

## 下一课

下一课实现 YAML 策略加载器，让 task_policy.yaml 真正成为 GuardPolicy 的来源。学习重点包括：

- 为什么配置不能只写死在 C++ 默认值中。
- yaml-cpp 在 ROS 2 ament_cmake 包中的依赖方式。
- 如何对缺字段、错误类型和空白名单失败关闭。
- 如何用测试证明加载出的策略确实控制 Guard。
