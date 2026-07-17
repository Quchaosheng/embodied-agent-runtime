# AI Mission Planner 设计

## 目标

将现有“模型把一句话转成单个任务”扩展为受限的任务级 Agent 闭环。
AI 在一次 mission 中参与三个环节：

1. 把中文意图生成最多三步的结构化计划。
2. 在非最终步完成或任一步失败后，从 Runtime 提供的受限转移中选择下一步。
3. mission 结束后生成只读的中文结果摘要。

模型仍不能输出坐标、速度、轨迹、BehaviorTree XML、重试次数或 ROS
消息。每个运动步骤都继续经过现有 `TaskRequest -> Task Guard -> ExecuteTask ->
Nav2` 边界。

## 实施分支与基线

- 本地分支：`feature/ai-mission-planner`。
- 基线：`feature/nav2-system-evidence`，因为它已提供真实 Nav2/TurtleBot3
  双目标系统证据。
- 只本地提交，不 push、不 merge；后续与 TaskEvent/rosbag 分支统一
  code review。

## 选择的方案

### 不选：只增加更多单轮 Prompt

这只会让意图解析看起来更复杂，但 AI 仍只在链路开头出现一次，无法体现
任务级闭环。

### 不选：端到端模型直接控制运动

这需要大量真机数据、实时推理和独立硬件安全链，也会破坏本项目“模型
不拥有运动权限”的核心价值。

### 选择：受限 Mission Agent

Gateway 拥有 mission 级计划和对话能力，现有 C++ Runtime 仍是每个运动步骤的
最终权威。这能同时展示 tool calling、Agent 状态、评测、ROS 2 Action 和
真实 Nav2 系统集成。

## 整体架构

```text
用户中文意图
  -> MissionModel.plan()                    AI 参与 1
  -> mission_plan.schema.json
  -> normalize_mission_plan()
  -> MissionRunner
       -> ExecuteTaskClient.execute(step 1)
       -> MissionModel.decide(state, choices) AI 参与 2
       -> transition guard
       -> ExecuteTaskClient.execute(step 2/3)
  -> MissionModel.summarize(trace)           AI 参与 3，只读
  -> CLI 结果 + 可评测 trace
```

新能力位于现有 `agent_gateway` ROS 包内，不创建第六个包，不改变
`ExecuteTask.action` 和 C++ Guard。

## Mission Plan 契约

模型只能通过单个 `submit_mission_plan` tool 返回 Draft 7 JSON：

```json
{
  "contract_version": 1,
  "steps": [
    {"action": "navigate", "target": "workbench", "deadline_s": 90},
    {"action": "navigate", "target": "dock", "deadline_s": 90}
  ]
}
```

严格约束：

- 根对象和 step 都为 `additionalProperties: false`。
- `contract_version` 只能为 `1`。
- `steps` 最少 1 步、最多 3 步。
- 第一版只允许现有 `navigate` 技能和 `dock` / `workbench` / `home`
  命名目标。
- 单步 deadline 继续为 1..90 秒，所有步骤 deadline 之和不得超过
  180 秒。归一化时将该总和固定为 mission 总预算。
- 拒绝相邻重复目标，防止无意义的连续重复 Goal。
- 模型不能提供 `task_id`；Runtime 为每步生成 mission-scoped ID。

归一化后使用不可变 `MissionPlan` / `MissionStep` dataclass。每个
`MissionStep` 再转换为现有 `NormalizedTaskRequest`，不绕过既有单任务校验。

## AI 参与 1：计划

`MissionModel.plan(user_text, schema)` 向 Provider 暴露一个闭合 Schema 工具。
模型必须恰好调用一次正确工具；零次、多次、错误工具、非字符串参数、
重复 JSON 键或 Schema 失败都会在 ROS Action 之前拒绝。

官方 OpenAI 和 OpenAI-compatible 中转继续共用现有 Chat Completions
协议边界，保留环境变量、HTTPS、1 MB 响应上限和 30 秒超时。本轮不引入
OpenAI SDK、LangChain 或其他 Agent 框架。

## AI 参与 2：检查点决策

每个 step 终态产生一个结构化 `MissionCheckpoint`，包含：

- 已完成和未完成的命名目标。
- 最后一步的 `goal_status`、`final_state`、`error_code`、`attempts` 和受限
  `detail`。
- Runtime 计算的允许转移集合。

模型只能通过 `select_mission_transition` 选择：

- `continue`：仅在上一步成功且仍有待执行步骤时提供。
- `return_home`：仅在没有正在运动、当前目标非 `home` 且剩余 mission
  deadline 允许时提供。Runtime 将它转成普通、受 Guard 校验的
  `navigate home`，deadline 为 `min(90, 向下取整的剩余总预算)`。它不是绕过
  安全边界的特权操作；home Goal 结束后 mission 终止，不恢复原计划。
- `abort`：始终提供，不产生新 ROS Goal。

Provider 返回不在当前允许集合内的选择时，Runtime 强制 `abort`。Provider 超时、
响应非法或无法连接时也强制 `abort`，不自动启动新运动。
最终计划步骤成功时 mission 直接成功，不为了增加 AI 调用次数而做无意义的
checkpoint 请求。

## AI 参与 3：结果解释

mission 终止后，`MissionModel.summarize(trace)` 只接收已完成的结构化 trace，
返回最多 500 字符的中文摘要。该响应不经过任何 Action Client，无法触发
运动。摘要超时或非法时，CLI 使用确定性本地摘要，不改变 mission 终态。

## 组件边界

### `mission_plan.py`

- 加载和缓存 mission Schema。
- 重复键拒绝、Draft 7 校验、总 deadline 和相邻目标校验。
- 产生不可变 `MissionPlan` / `MissionStep`。

### `mission_provider.py`

- `MissionModel` Protocol：`plan`、`decide`、`summarize`。
- `FakeMissionModel` 用于离线回归和 CI。
- `OpenAICompatibleMissionModel` 复用现有 URL、Key、超时和 transport 安全
  边界，但不扩展原有单任务 Provider 的职责。

### `mission_runner.py`

- 持有 `MissionModel` 和一个可调用的 step executor。
- 串行执行，任何时刻最多一个 `ExecuteTask` Goal。
- 使用 `time.monotonic()` 管理 mission 总 deadline，不为后续步骤重置预算。
- 生成 `MissionTrace`，保存每步请求、结果、AI 决策和最终原因。

### `mission_cli.py`

- 输入一条中文 mission。
- 显示归一化计划并要求人工确认；`--yes` 仅用于测试和自动 smoke。
- 流式显示每步 feedback、checkpoint 决策、最终 trace 和中文摘要。

## 数据流

1. CLI 读取用户文本并调用 `plan`。
2. 原始 tool arguments 经过严格 mission 归一化。
3. CLI 在任何 ROS Goal 之前展示计划并等待确认。
4. Runner 把当前 step 转换为 `NormalizedTaskRequest`，调用现有
   `ExecuteTaskClient.execute()`。
5. 非最终 step 成功或任一 step 失败后，Runner 计算允许转移，AI 只从
   集合中选择。
6. `return_home` 会执行一个普通 home Goal 并终止；无待执行 step、AI 选择
   abort、Runtime 错误或总 deadline 到期时直接终止。
7. summarize 产生只读解释。

## 失败处理

- 计划模型失败：拒绝 mission，不创建 ROS 节点和 Goal。
- 用户不确认：终止，不创建 Goal。
- ExecuteTask server 不可用：记录 step 失败，只允许 abort；不用 AI
  尝试绕过 ROS 错误。
- step 失败：AI 只能从 Runtime 根据当前状态生成的 abort/return_home
  集合中选择。
- checkpoint Provider 失败：强制 abort。
- 总 deadline 到期：不再发送新 Goal；当前 Goal 的取消仍由现有 executor 总期限
  语义管理。
- 摘要 Provider 失败：使用本地摘要，不改写执行结果。

## 测试设计

### 单元和协议测试

- Mission Schema 接受 1..3 个法定 step。
- 拒绝空计划、4 步计划、额外字段、重复键、未知目标、相邻重复目标和
  deadline 总和超 180。
- Provider 只暴露正确的单个 mission tool，拒绝零/多/错误 tool call。
- transition 拒绝当前集合外的选择。
- summarize 输出上限和确定性回退。

### Runner 状态测试

- 两步成功：执行顺序正确，每步产生唯一 task ID。
- AI 在第一步后 abort：第二步永不调用。
- 第一步失败后 return_home：仅产生一个普通 home Goal。
- Provider 超时、越权 transition 和 mission deadline 到期都不产生后续 Goal。
- 任何时刻最多一个 step executor 调用活动。

### 固定 AI 评测

新增 12 条 mission 语料：6 条法定多步计划，6 条必须拒绝的否定、越权、
四步、重复目标或 prompt-injection 输入。Fake model 必须 12/12；真实 Provider
使用相同语料，单独报告成功率、误接受率、平均延迟和请求数。

### 系统 smoke

默认 CI 使用 `FakeMissionModel` + Fake NavigateToPose，验证不联网的完整
mission 链路。本机真实系统 smoke 使用：

```text
“先去充电桩，再去工作台”
  -> plan: dock, workbench
  -> checkpoint: continue
  -> ExecuteTask(dock) -> real Nav2 -> SUCCEEDED
  -> ExecuteTask(workbench) -> real Nav2 -> SUCCEEDED
  -> AI summary
```

真实 OpenAI/中转运行必须显式 opt-in，密钥只来自环境，且先运行无 ROS
mission probe。没有用户 Key 时只宣称离线协议和 Fake model 证据。

## README 与学习交付

完成后同步更新：

- README 首屏架构、量化测试、12 条 mission 评测和两步真实 Nav2 证据。
- `agent_gateway/README.md` 的 mission CLI、Provider 配置和证据边界。
- 一课中文学习文档，解释计划、检查点、失败关闭和模型/运行时职责。
- 技术复习问答：为什么让 AI 多次参与，但仍不让它拥有运动权限。

## 明确不做

- 第一版不加语音、图像、VLM、SLAM、机械臂或真机驱动。
- 不加通用插件系统、动态 Python 代码执行或模型生成工具。
- 不让 summarize 输出触发新 mission。
- 不将软件 `SAFE_STOP` 宣称为硬件急停。
- 不在 CI 中使用真实 Key、花费模型费用或启动 Gazebo。
