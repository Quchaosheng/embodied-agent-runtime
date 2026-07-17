# GitHub 发布检查清单

## 上传前必须完成

- [ ] 5 个 `package.xml` 的 maintainer、公开邮箱和 license 已替换。
- [ ] `agent_gateway/setup.py` 的相同元数据已同步。
- [ ] 仓库根目录存在 LICENSE 文件，并与 package metadata 一致。
- [ ] `THIRD_PARTY_NOTICES.md` 区分当前依赖与规划/参考项目。
- [ ] `bash scripts/verify_release.sh` 完整通过。
- [ ] `git status` 不包含 `build/`、`install/`、`log/`、缓存或密钥。
- [ ] README 将已实跑的 Nav2 smoke 与未联网真实模型、未完成硬件集成分开表述。
- [ ] README 的 `123 tests`、单任务 `20/20` 和 Mission `12/12` 与最新本地输出一致。
- [ ] README 明确说明 checkpoint 失败不会发新 Goal，`return_home` 仍通过 Guard。
- [ ] 中转站、OpenAI 和 GitHub 凭据都没有进入文件或命令历史。

## GitHub 身份信息

项目只需要公开身份信息：

```text
GitHub 用户名
公开提交邮箱（可以用 GitHub noreply 邮箱）
许可证选择
```

不要把下面内容发给协作者或写进仓库：

```text
GitHub PAT / Token
SSH 私钥
OpenAI API Key
中转站 API Key
```

GitHub noreply 邮箱可在 `github.com` 的 Settings → Emails 中查看。它常见的形式是
`数字+用户名@users.noreply.github.com`，应以你页面实际显示的值为准。

## 本地 Git 初始化

Codex 不会自动替你提交或推送。发布门禁通过后，由你在本机执行：

```bash
cd /path/to/embodied-agent-runtime
git init -b main
git config user.name "你的 GitHub 用户名或公开姓名"
git config user.email "你的公开 GitHub 邮箱"
git add .
git status
git commit -m "feat: add safety-bounded embodied agent runtime"
```

提交前必须阅读 `git status`，确认没有 Key、`.env`、构建目录或私人材料。

## GitHub 登录与创建仓库

推荐 GitHub CLI：

```bash
gh auth login
gh repo create embodied-agent-runtime --public --source=. --remote=origin --push
```

`gh auth login` 会交互式打开浏览器或要求本机认证，不需要把 Token 发给 Codex。

若使用 SSH，应在本机生成密钥，并只把公钥上传 GitHub：

```bash
ssh-keygen -t ed25519 -C "你的公开 GitHub 邮箱"
```

可以复制 `~/.ssh/id_ed25519.pub`，绝不能复制 `~/.ssh/id_ed25519` 私钥。

## 上传后检查

- [ ] GitHub Actions 的 ROS 2 CI 为绿色。
- [ ] README 首页能直接看到中文速览、123 tests、12/12 Mission 和真实 Nav2 系统证据。
- [ ] 仓库 About 填写 ROS 2、embodied AI、Nav2、safety runtime。
- [ ] Topics 建议：`ros2`、`robotics`、`embodied-ai`、`nav2`、`cpp`、`python`。
- [ ] 默认分支为 `main`。
- [ ] 仓库中搜索 `API_KEY`、`github_pat_`、`ghp_`、`sk-`，确认只有变量名和示例。
- [ ] 首次 CI 成功后再把 CI badge 加入 README，避免使用错误仓库地址。
