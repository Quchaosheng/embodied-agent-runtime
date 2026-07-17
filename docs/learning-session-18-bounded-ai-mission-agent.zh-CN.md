# 学习第 18 课：受限 AI Mission Agent

## 这一课学什么

这次不是把机器人交给 AI，而是让 AI 在任务级闭环中参与更多，
同时保留原有运动安全边界：

```text
用户意图
  -> AI 生成受 Schema 约束的 MissionPlan
  -> Runtime 串行执行一个 ExecuteTask
  -> AI 只从 Runtime 提供的转移中选择
  -> 下一个 ExecuteTask 仍通过 C++ Guard
  -> AI 只读总结
```

关键句：**AI 拥有任务意图和受限决策权，不拥有运动权。**

## 三个 AI 参与点

### 1. 规划

`MissionModel.plan()` 把“先去充电桩，再去工作台”转成闭合 JSON：

```json
{
  "contract_version": 1,
  "steps": [
    {"action": "navigate", "target": "dock", "deadline_s": 90},
    {"action": "navigate", "target": "workbench", "deadline_s": 90}
  ]
}
```

Schema 只接受 1-3 步、三个命名目标、单步 1-90 秒、总预算不超过
180 秒。额外字段、重复 JSON 键、相邻重复目标和模型生成的 task ID
都会在 ROS 初始化前被拒绝。

### 2. 检查点决策

每个非最终步骤结束后，Runtime 根据实际结果计算当前允许集合：

- `continue`：上一步成功且仍有待执行步骤。
- `return_home`：当前不在 home，且至少剩余 1 秒总预算。
- `abort`：始终允许，不产生新 Goal。

AI 不能自己发明第四种选择。如果返回当前集合外的值，Runtime 按
`abort` 处理。

### 3. 只读总结

mission 终止后，`MissionModel.summarize()` 只接收已完成的 trace，输出最多
500 个字符。它没有 Action Client，不能发新 Goal。如果 Provider 超时或
输出非法，系统使用确定性本地总结，不改写原执行结果。

## 为什么最后一步成功不再调用 AI

最后一步成功时已经没有“是否继续下一步”的决策。再调用一次模型只会
增加延迟、费用和失败点，不会改变结果。因此 Runner 直接进入 `completed`，
然后进行一次只读总结。

## 为什么 Provider 失败必须终止

检查点时机器人已完成当前步骤，但下一步还没有 Goal。如果 Provider 断网、
超时或返回错误工具，系统不能“猜”应该继续，而是关闭失败：记录
`checkpoint_provider_failed` 并不发新 Goal。

## `return_home` 为什么不是后门

AI 只能选择字符串 `return_home`。Runner 会把它转成普通的
`NormalizedTaskRequest(action=navigate, target=home)`，为它生成 task ID，限制 deadline，
再调用原有 `ExecuteTask`。C++ Guard 仍会检查版本、目标、机器人忙磌状态、定位和
导航就绪状态。

## Fake 证据、真实 Provider 证据和 Nav2 证据

| 证据 | 能证明 | 不能证明 |
| --- | --- | --- |
| Fake 评测 12/12 | Schema、Runner、拒绝语义和评测器可重复 | 真实模型准确率 |
| OpenAI-compatible 离线协议测试 | URL、认证头、tool schema 和响应解析边界 | 真实站点可用或模型身份 |
| 真实 Nav2 mission smoke | 计划能经 Guard/ExecuteTask 驱动仿真导航 | 真机、keepout 强制或硬件急停 |

当前已有 Fake 评测、离线协议测试和真实 Nav2/Gazebo 证据；没有使用用户 Key
做真实模型联网结论。

## 一次有价值的系统调试

首次 Mission/Nav2 运行曾偶发导航失败。分层日志显示 AI 计划和 ExecuteTask 正常，
故障在 Nav2 `collision_monitor`：TurtleBot3 激光以 5 Hz 发布，周期正好是 0.2 秒，
而单源超时也是 0.2 秒。少量调度抖动会把正常扫描当成过期数据并停车。

修复没有增加 AI 重试或任务 deadline，而是将仿真的该参数重写为 0.5 秒，
并用配置回归测试和真实两步 mission 验证。这个案例说明了为什么要把
AI、Runtime 和 Nav2 分层观测。

## 必读代码

1. `agent_gateway/schema/mission_plan.schema.json`
2. `agent_gateway/agent_gateway/mission_plan.py`
3. `agent_gateway/agent_gateway/mission_provider.py`
4. `agent_gateway/agent_gateway/mission_runner.py`
5. `agent_gateway/agent_gateway/mission_cli.py`
6. `scripts/smoke_nav2_sim.sh`

## 技术复习练习题与答案

### 问题 1：AI 参与三次，为什么仍然可控？

答：三次参与的输出空间都是闭合的。计划只能生成最多三个命名目标；检查点
只能选 Runtime 当前允许的转移；总结没有运动接口。真正的每次运动都通过
现有 C++ Guard 和 ExecuteTask。

### 问题 2：如果模型要求直接去坐标 `(10, 20)` 怎么办？

答：Mission Schema 没有坐标字段，且 `additionalProperties` 为 false，因此计划在 ROS
节点或 Goal 产生前就会被拒绝。

### 问题 3：为什么不直接用 LangChain 或通用 Agent 框架？

答：当前只需要三个明确的模型调用点和一个小型状态机。使用现有 OpenAI-compatible
协议边界和 Python 标准库更容易审查运动权限，也避免新依赖。当真正需要多工具、
持久记忆或复杂对话编排时再评估框架。

### 问题 4：12/12 是否说明 AI 已经很准？

答：不是。这是 FakeMissionModel 的确定性离线基线，主要证明契约、拒绝路径和评测工具。
真实模型必须在同一语料上单独报告成功率、误接受数、延迟和请求数。

### 问题 5：这个项目有了 AI 后，ROS 2 工程价值是否被冲淡？

答：相反，AI 输出越不确定，越需要 ROS 2 Action 生命周期、C++ Guard、取消确认、
全局 deadline、有限恢复、Nav2 和仿真测试把它收敛成可验证的行为。AI 是上层意图模块，
不会替代机器人 Runtime 工程。
