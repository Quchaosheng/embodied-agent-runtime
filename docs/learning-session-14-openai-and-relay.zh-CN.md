# 第十四课：接入 OpenAI 与 OpenAI-Compatible 中转站

## 本课目标

这一课把云端模型配置拆成两个清晰 profile：

- `openai`：官方 OpenAI，使用标准 `OPENAI_*` 环境变量。
- `openai-compatible`：兼容中转站，使用项目自己的 `EMBODIED_AI_*` 变量。
- 两者共用 Chat Completions tool calling、严格 Schema 和 C++ Guard。
- 先用单请求探针验证模型，不启动 ROS Action。
- Key 不写文件、不放 README、不发送到聊天记录。

官方资料入口：

- [OpenAI Function Calling](https://developers.openai.com/api/docs/guides/function-calling)
- [OpenAI Latest Model Guide](https://developers.openai.com/api/docs/guides/latest-model)

当前 Codex 会话已配置官方文档 MCP，但工具不能在本会话热加载，直接读取官方页面又返回
403。因此本课没有根据缓存擅自写死“最新模型”，模型名必须填写你的账号或中转站实际
支持的值。重启 Codex 后可使用已安装的 `openaiDeveloperDocs` 再核对官方页面。

## 两个 Profile 如何分工

### 官方 OpenAI

默认地址：

    https://api.openai.com/v1

环境变量：

    EMBODIED_AI_PROVIDER=openai
    OPENAI_BASE_URL=https://api.openai.com/v1
    OPENAI_MODEL=<你的账号实际可用且支持 tools 的模型>
    OPENAI_API_KEY=<只存在当前 shell>

`OPENAI_BASE_URL` 可以不设置，代码会使用官方默认地址。模型名和 Key 必须显式提供。

### OpenAI-Compatible 中转站

环境变量：

    EMBODIED_AI_PROVIDER=openai-compatible
    EMBODIED_AI_BASE_URL=https://你的中转域名/v1
    EMBODIED_AI_MODEL=<中转站给出的模型名或别名>
    EMBODIED_AI_API_KEY=<中转站自己的 Key>

Base URL 可以是 `/v1` 根地址，也可以是完整 `/v1/chat/completions`；代码不会重复拼接。
远程地址必须使用 HTTPS。只有 `localhost`、`127.0.0.1` 和 `::1` 允许 HTTP，便于本地
Ollama 调试。

## 安全输入 Key

不要直接执行 `export KEY=明文`，因为明文可能进入 shell history。推荐：

### 官方 OpenAI

    export EMBODIED_AI_PROVIDER=openai
    export OPENAI_MODEL='<你的模型名>'
    read -rsp 'OpenAI API Key: ' OPENAI_API_KEY; echo
    export OPENAI_API_KEY

### 中转站

    export EMBODIED_AI_PROVIDER=openai-compatible
    export EMBODIED_AI_BASE_URL='https://你的中转域名/v1'
    export EMBODIED_AI_MODEL='<中转站模型名>'
    read -rsp 'Relay API Key: ' EMBODIED_AI_API_KEY; echo
    export EMBODIED_AI_API_KEY

`read -s` 不回显 Key。关闭终端后变量消失；不要把真实值写进
`provider.example.env`。

## 第一步：单请求、无机器人探针

构建并 source 后运行：

    cd ~/embodied_ws
    source /opt/ros/jazzy/setup.bash
    source install/setup.bash
    ros2 run agent_gateway probe_provider --provider openai '回充电桩'

中转站改为：

    ros2 run agent_gateway probe_provider --provider openai-compatible '回充电桩'

成功时应看到：

    Provider probe passed: action=navigate target=dock deadline_s=...
    No ROS Action was sent.

探针只调用一次模型并解析 Schema，不创建 ROS Node，不发送 ExecuteTask，因此适合第一次
联网。它仍可能产生一次 API 费用。

## 第二步：20 条模型评测

单请求成功后再运行：

    ros2 run agent_gateway evaluate_intents --provider openai

或：

    ros2 run agent_gateway evaluate_intents --provider openai-compatible

这会发送 20 次请求，可能收费。重点不只是总准确率，还要检查
`expected=REJECT actual=dock` 这类危险误接受。

## 第三步：真实 AI + 假 Nav2

先不要直接连接真实机器人。使用一个真实模型请求和确定性假导航：

    AI_SMOKE_PROVIDER=openai bash scripts/smoke_ai_gateway.sh

中转站：

    AI_SMOKE_PROVIDER=openai-compatible bash scripts/smoke_ai_gateway.sh

脚本默认仍固定使用 Fake Provider，只有显式设置 `AI_SMOKE_PROVIDER` 才联网，防止测试
时意外花费。

## 中转站的信任边界

中转站不是简单“换一个网址”。它可以看到：

- 用户自然语言指令。
- system prompt 和工具 Schema。
- 发给该站点的 Key。
- 模型响应及可能的使用记录。

因此不要把官方 OpenAI Key 交给陌生中转站，应使用中转站单独发放、可撤销、有限额的
Key。HTTPS 只能保护传输，不能证明站点不会保存数据，也不能证明它真的调用了宣传的
模型。

项目仍把中转站视为不可信输入：它不能提供坐标、速度、路径、重试次数或恢复策略；其
输出还必须经过响应大小限制、唯一工具检查、JSON Schema 和 C++ Guard。但如果中转站
故意把“回家”解释成合法的 dock，这些结构检查无法识别语义欺骗，所以还需要固定意图
评测、可信供应商和人工审查。

## 技术复习问题与答案

### 问：为什么不用官方 OpenAI Python SDK？

答：第一版只需要一个小型 Chat Completions tool-calling 边界，标准库 HTTP 让官方和
中转站共用同一协议，依赖更少，也便于注入内存 transport 做失败测试。以后若使用官方
Responses API、流式事件或 SDK 重试能力，可以新增官方 Provider，后面的 Schema、ROS
Action 和 Guard 不需要改变。

### 问：支持自定义 Base URL 会不会降低安全？

答：会扩大上游信任范围，所以代码拒绝远程 HTTP、URL 内嵌账号密码、query 和
fragment，并继续把所有响应当不可信数据。配置能力不等于信任中转站，供应商仍需审核。

### 问：为什么官方和中转站使用不同环境变量？

答：可以避免切换 profile 时误把官方 Key 发送到中转域名，也让故障信息明确指出缺少
哪套配置。官方使用常见 `OPENAI_*`，中转站使用项目命名空间 `EMBODIED_AI_*`。

### 问：为什么先 probe，再跑 20 条评测？

答：probe 只花一次请求，用来确认 URL、鉴权、模型名和 tool calling。它成功后再花
20 次请求评测语义，最后才接假 Nav2；这样成本低，失败也更容易定位。

## 本课完成状态

- [x] 官方 OpenAI profile 和默认官方 Base URL。
- [x] OpenAI-compatible 中转站 profile。
- [x] 远程 HTTPS 与 URL 凭据检查。
- [x] `/v1` 或完整 Chat Completions URL 兼容。
- [x] 单请求无 ROS 探针。
- [x] Offline smoke 默认显式 Fake，避免意外费用。
- [x] 当前总计 64 tests 通过。
- [ ] 用户在本机设置模型名与 Key。
- [ ] 真实 probe、20 条评测和 AI + 假 Nav2 smoke。
