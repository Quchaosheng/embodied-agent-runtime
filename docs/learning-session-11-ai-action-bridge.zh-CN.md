# 第十一课：Fake AI 到 ROS Action 的完整桥接

## 本课目标

这一课首次把自然语言接入真实 ROS 2 通信链路：

    中文用户输入
      -> FakeModelProvider
      -> 原始 JSON
      -> JSON Schema
      -> NormalizedTaskRequest
      -> ExecuteTask Action Client
      -> C++ Guard / executor
      -> fake NavigateToPose
      -> feedback / result

这里的“Fake AI”只替代模型推理，后面的 JSON、ROS Action、C++ Runtime 都是真实代码。

## Action Bridge 做什么

文件：

    agent_gateway/agent_gateway/action_bridge.py

它负责：

1. 把 `action="navigate"` 映射为 `ACTION_NAVIGATE=1`。
2. 把 NormalizedTaskRequest 填入 ExecuteTask Goal。
3. 缺少 task_id 时生成 UUID。
4. 把 Action feedback 转成稳定的 TaskProgress。
5. 把终态转成稳定的 TaskOutcome。
6. 本地等待异常时请求取消 outer Goal。

它不读取 target Pose，不调用 Nav2，也不决定 retry。

## 为什么 Task ID 由 Gateway 生成

模型契约允许省略 task_id。运行系统仍需要任务身份，用于：

- 日志关联。
- 用户界面显示。
- rosbag 和 TaskEvent 查询。
- 未来的取消和审计。

因此缺失时由确定性 Gateway 基础设施生成 UUID，而不是要求模型发明 ID。调用方已经
提供有效 ID 时则原样保留。

## Stable Feedback 与 Result

内部 ROS 消息先转换成 Python dataclass：

    TaskProgress(
      state_name="RUNNING",
      attempt=1,
      distance_remaining=3.0,
      detail="navigation in progress"
    )

    TaskOutcome(
      task_id="...",
      final_state_name="SUCCEEDED",
      error_code=0,
      attempts=1
    )

这样 CLI、网页或真实模型 provider 不需要到处解释数字常量。

## Fake Model Provider

文件：

    agent_gateway/agent_gateway/provider.py

当前离线映射：

| 用户关键词 | target |
| --- | --- |
| 充电、dock | dock |
| 工作台、workbench | workbench |
| 回家、home | home |

无法匹配时直接拒绝，不猜测新目标。Fake Provider 仍返回原始 JSON，并必须经过
`parse_task_request()`；它不能绕过 Schema。

## CLI 怎么运行

工作区构建后：

    ros2 run agent_gateway ask "电量低了，回充电桩"

当前端到端输出：

    AI selected: action=navigate target=dock deadline_s=30
    Feedback: state=RUNNING attempt=1 distance_remaining=3.00
    Feedback: state=RUNNING attempt=1 distance_remaining=2.00
    Feedback: state=RUNNING attempt=1 distance_remaining=1.00
    Result: ... state=SUCCEEDED error_code=0 attempts=1

一键复现脚本：

    bash scripts/smoke_ai_gateway.sh

脚本自动启动 fake Nav2 和 executor，执行中文 CLI，检查输出后清理进程。

## 测试证明了什么

新增测试覆盖：

- Action Goal 字段和常量映射。
- task_id 自动生成与保留。
- 不支持动作拒绝。
- RECOVERING feedback 转换。
- SAFE_STOP + error 34 result 转换。
- 三种中文意图映射。
- 未知意图拒绝。
- 对外 Schema 仍然关闭额外字段。

完整结果：

    Summary: 4 packages finished
    Summary: 55 tests, 0 errors, 0 failures, 0 skipped

## 为什么这还不算真实大模型

当前 Provider 是确定性关键词映射，不调用网络，也没有模型推理。它证明的是 provider
边界和 ROS 工程链路，而不是模型理解能力。

接入真实模型时只新增类似：

    class OpenAIProvider:
        generate_task(user_text, schema) -> raw_json

其他代码不变。真实 provider 返回的 tool arguments 仍必须经过相同 Schema。

## 真实模型接入前的选择

下一步需要选择一种 provider：

- OpenAI：云端 tool calling，效果稳定，需要 API Key。
- 通义千问：云端或兼容接口，需要对应 Key。
- Ollama：本地运行，无云端 Key，但需要下载模型且机器资源占用较大。

Key 只放环境变量，不写入仓库。选择 provider 不影响 C++ 安全代码。

## 技术复习问题与答案

### 问：Fake AI 有什么意义？

答：它把网络、费用和模型随机性排除，先证明自然语言入口、Schema、ROS Action 和
Runtime 的完整连接。真实模型出错时可以快速定位到 provider 层。

### 问：为什么 Action Bridge 在 Python，不放进 C++？

答：模型 SDK 和 Web/CLI 生态主要在 Python；安全校验和执行仍在 C++。Python 只做
协议适配，不能拥有坐标和恢复策略。

### 问：模型返回 tool arguments 后能直接构造 Goal 吗？

答：不能。必须重新经过 duplicate-key 检测和 JSON Schema，再由 Action Bridge 做
唯一的字符串到 ROS 常量映射。

### 问：Gateway 本地等待超时怎么办？

答：它会请求取消 outer ExecuteTask Goal 并返回 ActionBridgeError。Runtime 自己还有
独立的全局 deadline，因此 Gateway 网络或等待逻辑不能取消 C++ 安全边界。

### 问：AI 能自动看到失败后再发一次任务吗？

答：它可以解释 error 32 或 34，但不能自动无限重发。Runtime 已拥有固定恢复预算，
新的业务任务需要用户明确指令或受审查的上层规则。

## 本课完成状态

- [x] provider-independent ExecuteTask Action Client。
- [x] task_id 生成。
- [x] feedback/result 稳定对象。
- [x] Fake AI 中文 CLI。
- [x] AI 到 fake Nav2 的端到端 smoke。
- [x] 55 tests 全部通过。
- [x] OpenAI-compatible provider 离线协议测试。
- [ ] 真实模型联网验证。
- [ ] 用户认证、限流和审计事件。
