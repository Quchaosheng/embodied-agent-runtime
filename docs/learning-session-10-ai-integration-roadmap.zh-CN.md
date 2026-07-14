# 第十课：AI 如何接入安全机器人 Runtime

## 一句话结论

AI 只负责把自然语言解释成受限任务 JSON；Gateway、C++ Guard 和 executor 决定
这个任务能不能执行、如何执行、何时取消以及是否恢复。

正确的数据流：

    用户自然语言
      -> AI model tool calling
      -> 原始 JSON 文本
      -> agent_gateway 严格解析与 Schema 校验
      -> ExecuteTask Action Goal
      -> C++ task_guard 再校验
      -> task_executor
      -> Nav2

错误做法：

    AI -> x/y/yaw
    AI -> cmd_vel
    AI -> Nav2 Goal
    AI -> retry 次数或 Behavior Tree

## AI 在项目里的职责

AI 可以做：

- 理解“去充电”“回工作台”等自然语言。
- 在 `dock`、`workbench`、`home` 中选择一个命名目标。
- 选择唯一允许的 `navigate` 动作。
- 给用户解释任务结果。

AI 不可以做：

- 生成地图坐标、速度、路径或姿态。
- 修改 deadline 上限、恢复次数或取消策略。
- 绕过 Gateway 直接调用 ROS。
- 根据 prompt 动态放宽安全策略。

## 一个完整例子

用户说：

    电量低了，回充电桩

模型只能产生：

    {
      "contract_version": 1,
      "action": "navigate",
      "target": "dock",
      "deadline_s": 90
    }

Gateway 调用现有 `parse_task_request()`。合法后再映射为 ROS Action Goal：

    goal.contract_version = request.contract_version
    goal.action = ExecuteTask.Goal.ACTION_NAVIGATE
    goal.target = request.target
    goal.deadline_s = request.deadline_s

真正的 dock Pose 仍从 `simulation/config/targets.yaml` 加载，模型看不到坐标。

## 推荐的四步接入顺序

### 第一步：先做 ROS Action Bridge

在 `agent_gateway` 增加一个 provider-independent Action Client：

    normalized JSON
      -> ExecuteTask Goal
      -> feedback callback
      -> terminal result

这一阶段使用固定 JSON fixture 和假导航服务器，不需要模型 API Key。测试至少覆盖：

- navigate 字符串映射为 Action 常量 1。
- 缺少 task_id 时由 Gateway 生成唯一 ID。
- feedback 能转换成面向用户的状态。
- error 13、32、34 能转换成明确说明。
- 客户端取消能调用 outer Goal cancel。

### 第二步：增加 Fake Model Provider

定义非常小的模型接口：

    ModelProvider.generate_task(user_text, schema) -> raw_json

测试 provider 固定返回 JSON，不访问网络。这样先证明：

    用户文本 -> provider -> Gateway -> ROS Action -> result

更换模型时只替换 provider，不修改 C++ Guard、executor 或 Nav2。

### 第三步：接一个真实模型

可以选择：

- 云端模型：OpenAI、通义千问等 tool/function calling。
- 本地模型：Ollama 或兼容 OpenAI API 的本地服务。

模型工具参数直接复用 `task_request.schema.json` 的字段和限制。模型返回的 arguments
必须重新序列化为 JSON 文本，再经过 `parse_task_request()`；不能因为使用了 tool
calling 就跳过 Schema 校验。

API Key 只通过环境变量提供，例如：

    export OPENAI_API_KEY=...

Key 不写入 YAML、README、launch 文件或 Git 仓库。

### 第四步：增加用户入口

第一版建议做 CLI：

    ros2 run agent_gateway ask "回充电桩"

后续再增加 HTTP/WebSocket 或网页。入口层只负责：

- 接收用户文本。
- 展示模型选出的命名任务。
- 请求用户确认运动任务。
- 展示 feedback、result 和错误码。

不要让网页或 HTTP 服务直接发布 `/cmd_vel`。

## 为什么现在就能接 AI

AI 集成不需要等待真实 Nav2。当前假服务器已经使用真实
`nav2_msgs/action/NavigateToPose` 协议，所以可以先完成：

    AI -> Gateway -> ExecuteTask -> fake NavigateToPose

将来启动真实 Nav2 后，只替换内层 Action Server，AI、Gateway、契约和 Guard 都不
需要修改。这正是双层 Action 架构的价值。

## Result 如何返回给 AI

建议先转换成稳定的业务对象：

    {
      "task_id": "...",
      "state": "SUCCEEDED",
      "error_code": 0,
      "attempts": 1,
      "detail": "navigation succeeded"
    }

AI 可以把这个对象解释成人话，但不能根据失败结果自动发起无限新任务。

例如：

- error 13：目标不在允许列表。
- error 32：任务超过全局 deadline，内层取消已确认。
- error 34：两次导航均失败，Runtime 进入 SAFE_STOP。

是否重新尝试必须由 Runtime 静态策略或用户新指令决定，不由模型自行循环。

## 生产环境需要的保护

在模型接入后增加：

1. 用户身份验证与权限。
2. 请求频率限制，同一机器人一次只允许一个任务。
3. 运动前的人类确认开关。
4. 原始模型输出、规范化请求、Guard 结果和最终结果审计日志。
5. 模型调用 timeout 和网络失败处理。
6. 禁止任意工具、shell、ROS topic 和文件访问。
7. API Key 使用 secret/environment 管理。

模型 prompt 不是安全边界。即使 prompt 写着“不要输出坐标”，Schema、Gateway 和
C++ Guard 仍必须拒绝坐标字段。

## 技术复习问题与答案

### 问：这个项目里的 AI 是规划器吗？

答：不是。AI 是意图解释器，只把自然语言收敛为固定任务契约。任务级安全由 C++
Runtime 决定，运动规划和控制由 Nav2 决定。

### 问：Tool calling 已经保证结构化，为什么还要 Schema？

答：模型服务、SDK 和调用方都可能产生错误或被绕过。Tool calling 提高格式正确率，
Schema 和 C++ Guard 才是确定性的失败关闭边界。

### 问：更换 OpenAI、千问或本地模型要改 C++ 吗？

答：不需要。不同 provider 只实现 `generate_task()`，统一返回原始 JSON。后面的
Gateway、Action、Guard、executor 和 Nav2 保持不变。

### 问：模型能看到机器人实时 feedback 吗？

答：界面可以把 feedback 展示给用户，也可以让模型做自然语言解释；但模型不能
根据 feedback 直接生成速度或无限追加任务。

### 问：为什么先做 Fake Provider？

答：它无网络、无费用、可重复，可以先证明 AI 边界到 ROS Action 的工程链路。
接真实模型后若失败，就能区分 provider 问题和 Runtime 问题。

## 下一步代码任务

最合理的下一次实现是：

1. `agent_gateway/execute_task_client.py`：NormalizedTaskRequest 到 ROS Action。
2. Action Client 单元与 launch test。
3. `FakeModelProvider` 和一条端到端 CLI。
4. 最后再由你选择 OpenAI、千问或本地 Ollama provider。

## 本课完成状态

- [x] AI 与 Runtime 的职责边界明确。
- [x] provider-independent 接入顺序明确。
- [x] API Key 和生产安全要求明确。
- [x] Gateway ExecuteTask Action Client。
- [x] Fake Model Provider 端到端 CLI。
- [ ] 真实模型 provider。
