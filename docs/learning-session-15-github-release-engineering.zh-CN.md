# 第十五课：把项目做到可上传 GitHub

## 本课目标

“能上传 GitHub”不只是执行 `git push`。一个技术复习项目至少要回答：

- 陌生开发者能不能从干净环境构建？
- 测试是否自动运行，而不是只有一张本地截图？
- README 能否在一分钟内说明工作量和安全边界？
- 仓库是否包含 Key、构建垃圾或虚假完成度？
- 后续修改有没有贡献规则和发布门禁？

## 本次增加的工程化内容

### 1. GitHub Actions

`.github/workflows/ros2-ci.yml` 在 Ubuntu 24.04 容器中安装 ROS 2 Jazzy，然后：

1. 用 rosdep 安装 package manifests 声明的依赖。
2. 从空工作区构建四个 ROS 包。
3. 运行 C++、Python 和 launch tests。
4. 运行 20 条 Fake Provider 中文意图评测。
5. 运行 Runtime 与 AI Gateway 两个进程 smoke。

CI 不配置模型 Key，只使用 Fake Provider，因此不会产生模型费用，也不会连接真实硬件。

### 2. 本地发布门禁

上传前运行：

```bash
bash scripts/verify_release.sh
```

它除了重复构建和测试，还检查：

- 发布需要的文档与配置是否存在。
- package metadata 是否还保留 placeholder。
- 是否出现常见 GitHub/OpenAI Key 格式。
- 是否混入超过 10 MB 的文件。
- 两个进程 smoke 是否都能清理后台节点。
- 每个 smoke 是否使用独立 ROS Domain，避免 DDS 发现残留相互干扰。

### 3. 仓库治理文件

- `CONTRIBUTING.md`：修改安全关键代码时必须遵守的规则。
- `SECURITY.md`：凭据处理、漏洞报告和非认证安全范围。
- `CHANGELOG.md`：已完成与未完成能力分开记录。
- Pull Request template：要求提交测试证据和安全影响。
- `.gitignore`：排除 ROS 构建目录、缓存、环境文件和私钥。

## 如何体现工作量

技术复习时不要只说“写了很多代码”，而要说可验证产物：

> 我把项目拆成 4 个 ROS 2 包，完成外层任务 Action 和内层 Nav2 Action 的双层生命周期；
> 64 项测试覆盖 C++ Guard、Python Gateway、配置失败关闭和 9 个进程级 launch 用例；
> 另外有 20 条中文意图评测和两个端到端 smoke。仓库提供 Ubuntu 24.04/ROS Jazzy CI，
> 可以从干净工作区重复构建，而不是只在我的电脑上运行。

这比 LOC 更有说服力，因为测试、失败路径和可复现环境能证明代码确实被执行过。

## GitHub 和极狐 GitLab 的区别

本项目目标平台是 **GitHub：`github.com`**，不是极狐 GitLab/Jihu。对应工具是：

```bash
gh auth login
gh repo create
```

不使用 `jihulab.com`、GitLab Access Token 或 `glab` 命令。

## 你需要准备什么

只准备三个可公开信息：

1. GitHub 用户名。
2. 公开提交邮箱，推荐 GitHub noreply 邮箱。
3. 开源许可证选择；技术复习型 ROS 2 项目通常可以考虑 Apache-2.0，但必须由仓库所有者决定。

不要把 GitHub Token、PAT 或 SSH 私钥发给 Codex。认证应通过本机 `gh auth login` 或 SSH
完成。

## 为什么许可证不能由 AI 擅自决定

许可证决定别人复制、修改、分发代码的法律条件。Apache-2.0、MIT 和“不开放许可”含义
不同，AI 可以解释差异，但仓库所有者必须作出最终选择。没有 LICENSE 时，公开可见不等
于别人获得使用许可。

## 技术复习问题与答案

### 问：CI 比本地测试多证明了什么？

答：本地环境可能残留已安装依赖、旧 build cache 或手工配置。CI 从干净 Ubuntu/ROS 环境
重新 rosdep、构建和测试，能发现“只在我电脑上能跑”的隐式依赖。

### 问：为什么 CI 不连接真实 OpenAI？

答：公开仓库不应为了普通 PR 暴露付费 Key 或产生不可控费用，而且模型响应具有波动。
CI 使用 Fake Provider 验证确定性协议和 Runtime；真实模型用受控的手工 probe 与固定语料
评测，结果单独记录。

### 问：64 tests 是否都等于真实机器人测试？

答：不等于。它们包含单元、配置、协议和进程级 Action 测试，证明 Runtime 语义；真实
Nav2/TurtleBot3 仍是单独的系统集成里程碑。准确区分测试层级比夸大数字更专业。

### 问：为什么需要 SECURITY.md？

答：项目接入模型 Key 和机器人任务，贡献者必须知道哪些信息不能公开，以及软件 Guard
不等于硬件急停或认证安全系统。明确边界可以减少误用。

## 当前完成状态

- [x] GitHub Actions ROS 2 Jazzy workflow。
- [x] 本地完整发布门禁。
- [x] CONTRIBUTING、SECURITY、CHANGELOG、PR template。
- [x] README 中文速览和量化工程证据。
- [x] GitHub 上传与密钥安全清单。
- [x] 仓库所有者提供公开用户名、邮箱和 Apache-2.0 决定。
- [x] 替换 ROS metadata 并加入 LICENSE。
- [ ] 本机初始化 Git、提交并通过 `gh auth login` 上传。
- [ ] GitHub 首次 CI 绿色后补真实 badge。
