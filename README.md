# Pi Desktop

基于 Qt 6.11、C++20 和 Qt Quick/QML 的 Pi 桌面客户端初版。

## 已实现

- 通过 `QProcess` 启动 `pi --mode rpc`
- 严格按 LF 解析 JSONL RPC 数据
- Prompt 发送与 Abort
- 输入框 `/` 触发命令补全，聚合 Pi 上报的扩展命令 / 提示模板 / 技能与桌面端内置命令
- 桌面端内置斜杠命令：`/new`、`/compact`、`/model`、`/thinking`、`/name`、`/export`、`/abort`，分别映射到对应 RPC
- Prompt 附件：`+` 按钮或拖放添加，文本按 `<file>` 块展开、图片转 base64 随 Prompt 发送
- Assistant 流式消息展示
- Markdown 渲染
- Tool Call 状态、输入、累计输出和折叠卡片；展开后的 Input / Output 各自带独立滚动条，长内容可拖动或滚轮查看
- Pi 历史 Session 后台增量扫描、新建与切换
- 会话栏按“项目 → 工作文件夹 → 会话”分组，支持自定义名称、目录选择、重命名及独立持久化
- Pi 扩展的 select、confirm、input、editor、notify、setStatus 和编辑器预填协议
- 默认使用 `~/.pi/profiles/Desktop`，支持自动发现及下拉选择其他 Pi Profile
- 自动发现 Pi 程序路径与 Profile 目录，支持下拉选择、手动输入、浏览和重新扫描
- Pi 命令与 Agent 工作目录配置
- 进程异常检测和有限次数自动重启
- 左右侧栏独立折叠，新会话创建成功后默认收起；忙碌期间仍可滚动和搜索历史会话
- 会话栗三级层级：项目为分组标题（深色加粗 + 分组底色 + 组间间距），工作文件夹与会话用 16 / 34 像素缩进加层级引导线区分，当前工作目录额外带左侧强调色条
- 输入框高度按行数自动增长，最高到窗口高度的一半，再高改为框内滚动且框高不再变化
- 扩展请求、连接设置、模型/思考选择弹窗按窗口尺寸自适应：宽高均留出窗口边距，内容超出时在弹窗内部滚动，底部按钮始终可见
- 模型错误详情、重试等待、压缩状态及有界 stderr 诊断卡片
- 当前上下文占用、会话累计 token、缓存用量和费用（以 Pi 返回值为准，缺失显示未知）
- 工作提示每 10 秒从 20 个英文词中随机切换且不连续重复，附带依次跳动的三点动画；结束或断开后停止
- ChatModel、Agent RPC 与 QML 交互回归测试

Pi Session 始终是会话事实源；桌面端只负责展示与读取元数据。

## 构建

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt-6.11-kit-path> -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

使用 MinGW 时，CMake、编译器和 Qt 必须来自兼容的 Kit。

## 运行

确保 `pi` 和 `node` 位于 `PATH`，然后运行构建生成的 `pi_Desktop`。

Windows 版使用 GUI 子系统，双击或右键菜单启动不会附带 CMD 窗口；后台 Node/Pi 及 CMD 兼容启动路径也使用无控制台模式，RPC 管道和错误处理不受影响。主动运行菜单安装/卸载 `.cmd` 脚本时仍会显示操作提示。

应用启动 Pi 时会设置：

```text
PI_CODING_AGENT_DIR=%USERPROFILE%/.pi/profiles/Desktop
PI_CODING_AGENT_SESSION_DIR=%USERPROFILE%/.pi/profiles/Desktop/sessions
```

聊天输入使用 **Enter 发送、Shift+Enter 换行**。输入框高度随行数自动增长，最高到窗口高度的一半（`page.height * 0.5`），超过后改为框内滚动、框高保持不变。

输入框输入 `/` 会弹出命令补全（↑/↓ 选择、Enter/Tab 确认、Esc 关闭），列表同时包含 Pi 的扩展命令、提示模板、技能，以及桌面端内置命令：

| 命令 | 作用 |
| --- | --- |
| `/new` | 新建会话 |
| `/compact` | 手动压缩上下文 |
| `/model` | 弹窗选择模型 |
| `/thinking` | 弹窗设置思考等级 |
| `/name <名称>` | 重命名当前会话 |
| `/export` | 导出会话为 HTML |
| `/abort` | 中止当前任务 |

内置命令由桌面端本地拦截并映射到 RPC，不会作为消息发送给 Pi；未命中的斜杠命令（扩展命令、提示模板、技能）仍原样交给 Pi 处理。Pi 的 TUI 内置命令（如 `/reload`）不会通过 RPC 下发，其中 `/reload` 暂无对应 RPC，需要重载上下文或扩展时可用重启 Pi 连接代替。忙碌期间 `/compact`、`/model`、`/thinking` 等会提示任务结束后再执行。

输入框左侧的 **+** 或直接把文件拖入输入框可添加附件，输入框中会以芯片形式显示并支持逐个移除。文本文件按 Pi CLI 约定包成 `<file name="...">内容</file>`，图片识别为 PNG/JPEG/GIF/WebP/BMP 后以 base64 随 Prompt 发送并附文件名引用；单文件上限 10 MiB，发送成功后自动清空。

会话栏“项目”标题旁的 **+** 用于创建自定义项目；每个项目旁的 **+** 可添加真实工作文件夹，添加成功后自动切换当前工作目录并重连 Agent。忙碌或切换期间禁止添加，添加失败不切换目录。项目和工作文件夹末尾的 **×** 可删除列表项，**···** 菜单仍可重命名或删除；删除必须二次确认，保存失败时保留确认框和错误提示。项目配置独立于 Profile，会话按自身的 `cwd` 匹配文件夹；未归入项目的历史保留在“未分组”，不会显示 Profile 的编码存储目录名。移除项目或文件夹不会删除任何磁盘文件或会话。

点击工作文件夹或历史会话会同步切换实际工作目录：应用保存目录并重启自己持有的 RPC 子进程，历史会话通过 `--session` 恢复，新进程以所选目录作为 `cwd`。切换完成前禁止发送消息；忙碌、目录不存在或会话目录不匹配时拒绝切换，不会静默回退到其他目录。排故日志前缀为 `[Projects]`、`[ProjectWorkspace]`。

首次启动后也可通过右上角设置按钮配置：

- Pi 命令，例如 `pi` 或 `C:/Users/.../npm/pi.cmd`
- 后台 Pi 代理：例如 `http://127.0.0.1:7890`（使用代理软件实际 HTTP/Mixed 端口），支持 HTTP/HTTPS，不支持 SOCKS。保存并重连后向 Pi 子进程注入大小写 `HTTP_PROXY`、`HTTPS_PROXY`、`ALL_PROXY`；保留 `NO_PROXY` 绕过规则。留空继承系统环境及 Pi 自身配置，不表示强制直连。不修改系统代理或 Profile 文件，适用于桌面端所有工作目录/Profile。地址以明文保存到本机 QSettings，请避免填写敏感密码。
- Pi Agent 工作目录
- Pi Profile 目录：从下拉列表选择，也可直接输入或通过“浏览”自定义；点击“保存并重连”后生效

启动和每次打开设置时会自动扫描：Pi 程序从 `PATH` 和 Windows `%APPDATA%/npm` 中发现；Profile 从 `~/.pi/agent`、`~/.pi/profiles/*`、`PI_CODING_AGENT_DIR` 和当前已保存目录中发现。自定义目录位于 `profiles` 下时也列出其同级目录。只检查目录、不递归扫描会话、不读取认证内容；无效目录跳过，重复路径合并。空 Profile 目录也可选择，不强制要求已有 `settings.json` 或 `auth.json`。

“重新扫描”保留尚未保存的输入，不会自动切换 Profile 或覆盖设置。Pi 程序和 Profile 下拉列表每项末尾均有 **×**，点击并二次确认后从候选中永久隐藏，重扫和重启不会自动恢复；取消确认不修改列表。删除不卸载程序、不删除 Profile 文件、不终止或改变当前连接，即使删除正在使用的候选也保留编辑框的当前路径。仍可手动输入或浏览已隐藏/未扫描到的路径；保存时沿用已存在、可写绝对目录的校验。扫描和删除诊断前缀为 `[PiDiscovery]`、`[ConnectionOptions]`。

应用只管理自己创建的一个 Pi RPC 子进程，不会扫描或终止系统中的其他 Pi 进程。

为避免长会话卡顿，Session 元数据在 `QtConcurrent` 工作线程扫描，单文件仅读取头尾区域，并按文件大小和修改时间复用增量缓存。Assistant 和 Tool 流式更新也会合并后再通知 QML，折叠的 Tool Output 不创建文本渲染组件。

## Windows 文件夹右键启动

在发布目录的 `bin` 中双击 `install-folder-menu.cmd`，即可为当前用户安装“在 Pi Desktop 中打开”菜单，无需管理员权限。支持文件夹空白处及文件夹本身的右键；Windows 11 可能需要先点“显示更多选项”。双击 `uninstall-folder-menu.cmd` 可移除菜单。移动程序后需在新位置重新安装菜单，删除程序前建议先卸载菜单。

也可直接运行：

```powershell
.\pi_Desktop.exe --install-folder-menu
.\pi_Desktop.exe --uninstall-folder-menu
.\pi_Desktop.exe --workspace "D:\项目目录"
```

右键启动会打开独立窗口，把应用和 Pi Agent 的工作目录设置为所选文件夹，并更新已保存的工作目录；不复用其他窗口的会话。目录不存在或不可访问时拒绝启动，不回退到旧目录。普通启动继续沿用已保存配置。排故日志前缀为 `[FolderMenu]`。

## 交互与排故

- 右侧默认仅显示 Context。`ContextPanel.outlineContent` 和 `filesContent` 为预留组件接口，默认 `null` 时不实例化内容、不占布局；原 `OutlineItem` / `FileItem` 展示组件保留供后续开发。
- 英文词定时切换不会覆盖工具、重试或错误状态；跳动动画只表示本地界面活动，不代表模型服务端心跳。

- 流式回复先按纯文本展示，结束后渲染 Markdown，避免每个增量重新解析整段文档。
- RPC 事件保持顺序，按最多 64 条 / 4 毫秒的时间片分派，让出 UI 线程处理输入；单条事件处理时间不受该预算硬性中断。
- 上翻阅读历史时暂停自动滚底，回到底部后恢复跟随。
- `[AgentTiming]` 在整轮结束时记录 `firstEventMs`、`firstTextMs` 和 `totalMs`。这些是从提交开始的端到端耗时，包含 Pi 扩展、网络、模型推理和工具等待，不能当作纯 RPC 传输耗时。
- 上下文占用与累计 token 不相同；统计在启动、会话切换、回合结束、压缩及整轮结束时更新，不逐 token 轮询。压缩后的未知占用显示 `Context —`，完整统计可展开右侧查看。
- stderr 仅保留最近 16 KiB，批量更新同一诊断卡片；stderr 本身不一定意味着请求失败。

## 当前范围

本版本对应技术方案中的 MVP。暂未实现 SQLite FTS5 全文搜索、消息跳转、分支树、多 Agent、WebEngine 和代码高亮。
