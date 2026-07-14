# 第十二课：OpenAI-Compatible Tool Calling Provider

## 本课目标

这一课增加一个可连接多种服务的模型适配器，但不改变机器人安全边界：

- 使用 OpenAI-compatible `/chat/completions` tool calling 协议。
- base URL、model、API Key 和 timeout 全部来自环境变量。
- 只暴露一个 `submit_robot_task`，明确意图最多调用一次。
- tool arguments 继续经过严格 JSON 和 Schema。
- 离线测试不访问网络、不产生费用。

## 为什么做 Compatible Provider

许多云端和本地服务提供相似的 Chat Completions 接口。项目只需要维护一个协议适配
层，就能连接：

- 支持该兼容协议的云端模型服务。
- 通义千问等兼容模式。
- Ollama 等本地兼容服务。

这不代表所有模型或所有服务版本都支持 tool calling。联网前仍要确认所选模型支持
function/tool calls。

## 环境变量

配置样例位于：

    agent_gateway/config/provider.example.env

实际运行时在本机 shell 设置：

    export EMBODIED_AI_PROVIDER=openai-compatible
    export EMBODIED_AI_BASE_URL=<provider-v1-base-url>
    export EMBODIED_AI_MODEL=<provider-model-name>
    export EMBODIED_AI_API_KEY=<secret-if-required>
    export EMBODIED_AI_TIMEOUT_S=30

然后执行：

    ros2 run agent_gateway ask --provider openai-compatible "回充电桩"

API Key 不写入仓库、不粘贴到 README，也不建议发到聊天记录。只在用户本机环境中
导出；不需要 Key 的本地服务可以留空。

## Provider 发送什么

适配器向：

    <base_url>/chat/completions

发送：

- system message：模型只能解释意图；只有明确的单目标请求才调用工具。
- user message：原始用户文本。
- tools：唯一的 `submit_robot_task`。
- tool_choice：`auto`，不支持、否定或多目标输入应不调用工具。
- temperature：0。

工具 parameters 复用项目 JSON Schema，并保留：

    additionalProperties: false
    target enum: dock, workbench, home
    deadline_s: 1..90

`$schema` 和文档标题会从请求参数中移除，以提高不同兼容服务的接受度；核心约束不变。

## Provider 接受什么响应

响应必须满足：

1. 明确任务必须存在 `choices[0].message.tool_calls`。
2. tool_calls 必须恰好一个；零个按不支持的意图拒绝，多个也拒绝。
3. function name 必须为 `submit_robot_task`。
4. arguments 必须是 JSON 文本。
5. HTTP 响应不能超过 1 MB。

arguments 返回后仍调用：

    parse_task_request(arguments)

因此模型即使返回 x、velocity、第四个 target 或 deadline 91，Gateway 仍会拒绝。

## 网络错误如何处理

以下情况转换为 ProviderError，不进入 ROS Action：

- HTTP 错误。
- DNS 或连接失败。
- 请求 timeout。
- 返回体不是 UTF-8 JSON。
- 返回体超过 1 MB。
- 缺少、重复或错误的 tool call。

模型服务失败不会自动降级成“猜一个 dock”，也不会绕过 Schema。

## 离线测试设计

Provider 构造函数允许注入 transport。测试传入内存函数，检查：

- 最终 URL 为 `/chat/completions`。
- Authorization Header 格式正确。
- timeout 正确传递。
- tool name 唯一。
- tool parameters 保留关闭字段规则。
- 缺少 tool call 会拒绝。
- 多个 tool call 会拒绝。
- `file://` 等非 HTTP URL 会拒绝。
- 环境变量能够构造 provider。

这些测试验证协议和安全解析，但不声称真实服务已经联网成功。

## 当前证据

    Summary: 4 packages finished
    Summary: 59 tests, 0 errors, 0 failures, 0 skipped

默认 Fake Provider smoke 仍然通过：

    bash scripts/smoke_ai_gateway.sh

## 三种实际接法

### 云端 OpenAI-compatible 服务

使用服务商提供的 v1 base URL、支持 tools 的 model name 和 API Key。具体 URL、模型名
和可用参数必须以该服务当前官方文档为准。

### 通义千问兼容模式

使用兼容模式的 base URL、模型名和 DashScope Key。仍然走相同 Provider 和 Schema，
不修改 C++ Runtime。

### 本地 Ollama

通常使用本地兼容地址：

    EMBODIED_AI_BASE_URL=http://localhost:11434/v1

需要先在本机安装 Ollama、下载支持 tool calling 的模型，并把模型名写入
`EMBODIED_AI_MODEL`。本地服务通常不需要 API Key，但会占用内存和显存。

## 技术复习问题与答案

### 问：为什么不直接使用某厂商 SDK？

答：Compatible Provider 减少厂商耦合，适合第一版契约验证。若未来使用厂商特有的
Responses、流式事件或鉴权能力，可以新增 provider，但后面的 Schema 和 ROS bridge
不变。

### 问：temperature=0 就能保证安全吗？

答：不能。它只降低随机性，不保证输出合法。唯一工具、Schema、duplicate-key 检测
和 C++ Guard 才是确定性边界。

### 问：为什么不能强制模型始终调用 tool？

答：用户可能说“不要去充电桩”或提出项目不支持的任务。如果强制调用，模型可能被迫
从白名单中猜一个目标，反而产生语法合法但语义错误的运动。现在只有明确的单目标请求
才允许调用；零个、多个或错误 tool call 都不会进入 ROS Action。

### 问：为什么限制响应为 1 MB？

答：任务 JSON 只有几个字段。超大响应没有业务价值，限制大小可以防止错误服务或
恶意 endpoint 消耗无限内存。

### 问：这个测试证明模型效果了吗？

答：没有。它证明 HTTP/tool-call 协议适配和失败关闭解析。模型对真实中文指令的效果
必须在选择服务后用固定语料集单独评估。

## 本课完成状态

- [x] OpenAI-compatible Chat Completions adapter。
- [x] 环境变量配置和无 Key 样例。
- [x] 单一闭合 Schema 工具，明确任务最多调用一次。
- [x] HTTP/JSON/响应大小失败关闭。
- [x] 6 个兼容 provider 离线测试。
- [x] 总计 59 tests 通过。
- [ ] 选择真实服务与模型。
- [ ] 使用真实 Key 做联网 smoke。
- [x] 固定 20 条中文意图评估集。
