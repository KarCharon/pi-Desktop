# Pi Desktop UI 设计文档

## 1. 文档目的

本文用于定义基于 **Qt 6.11 + pi** 的桌面客户端 UI 设计规范。

产品定位：

> 一个以 pi 作为 Agent / 对话后端，以 Qt 6.11 作为桌面 UI 层的 AI Coding Client。

UI 设计方向参考 Claude Desktop / Claude Web 的视觉语言，但不进行像素级复刻。

核心目标：

- 提高长对话的阅读效率
- 提高历史 Session 的查找效率
- 支持 Markdown / Code Block / Tool Call 展示
- 支持对话内快速定位历史内容
- 弱化 UI 装饰，突出内容本身
- 保持桌面工具软件的稳定感和信息密度
- 为后续文件、工具调用、上下文管理预留扩展空间

---

# 2. 设计关键词

整体设计关键词：

```text
Minimal
Warm
Content First
Low Visual Noise
Developer Oriented
Desktop Native
```

设计原则：

> UI 尽量退到内容之后。

Pi Desktop 不采用典型即时通讯软件的“强聊天气泡”设计，而更接近持续生成的技术文档阅读界面。

例如：

```text
User
────────────────────────────────

帮我分析这个函数为什么会阻塞。


Assistant
────────────────────────────────

从这段代码来看，主要存在三个问题：

1. ...
2. ...
3. ...

void Foo()
{
    ...
}

Tool Call
────────────────────────────────

Read src/Foo.cpp
```

---

# 3. 总体布局

## 3.1 推荐窗口尺寸

推荐默认窗口：

```text
1440 × 900
```

最低支持：

```text
1024 × 720
```

布局模式：

```text
Sidebar + Conversation + Optional Right Panel
```

整体结构：

```text
┌──────────────────────────────────────────────────────────────────────┐
│                              Title Bar                               │
├──────────────┬───────────────────────────────────────┬───────────────┤
│              │                                       │               │
│   Sidebar    │             Conversation              │  Right Panel  │
│              │                                       │   Optional    │
│   Sessions   │                                       │               │
│   Search     │                                       │   Outline     │
│   Projects   │                                       │   Files       │
│              │                                       │   Tools       │
│              │                                       │   Context     │
│              │                                       │               │
│              │                                       │               │
│              ├───────────────────────────────────────┤               │
│              │               Composer                │               │
├──────────────┴───────────────────────────────────────┴───────────────┤
│                              Status Bar                              │
└──────────────────────────────────────────────────────────────────────┘
```

右侧 Panel 默认关闭，按需展开。

---

## 3.2 弹窗尺寸与滚动

自绘弹窗都按窗口尺寸自适应，并给窗口留出边距：

```text
Pi 扩展请求（Popup）    宽 min(窗口宽 - 140, 900)
                             高 min(窗口高 - 140, 内容高 + 36)
连接设置（Dialog）      宽 min(窗口宽 -  80, 760)
                             高 min(窗口高 - 120, 内容高)
模型 / 思考选择          宽 min(窗口宽 -  80, 560)
                             高 min(窗口高 - 120, 460)
```

规则：

- 内容超出可用高度时在弹窗内部滚动，底部操作按钮始终可见。
- 弹窗内边距归零，位置完全由内容区外边距控制，避免样式默认 padding 造成高度溢出。
- 说明文本、长标题不做无上限增长：标题限行省略，说明区限制高度后滚动。

---

# 4. Sidebar 设计

## 4.1 尺寸

默认宽度：

```text
260 px
```

可调整范围：

```text
220 ~ 320 px
```

折叠状态：

```text
56 px
```

## 4.2 信息结构

实现为三级层级：项目 → 工作文件夹 → 会话；搜索命中时展示匹配项及其祖先。

```text
┌────────────────────────┐
│ + 新建对话             │
│ 🔍 搜索项目、文件夹…   │
├────────────────────────┤
│ ▾ 项目 A      + × ···  │  ← 层级 1：分组标题
│   │ 工作文件夹 1       │  ← 层级 2
│   │   │ 会话 a         │  ← 层级 3
│   │   │ 会话 b         │
│   ▸ 工作文件夹 2       │
│ ▾ 项目 B               │
│   │ 工作文件夹 3       │
├────────────────────────┤
│ 工作目录：…            │
│ ⚙ 设置                │
└────────────────────────┘
```

层级视觉规则（三个层级必须一眼可区分）：

| 层级 | 行高 | 缩进 | 标题 | 底色 |
| --- | --- | --- | --- | --- |
| 项目 | 36 | 2 | 12 px 加粗、深色分组标题色 | 分组底色 + 行前 8 px 间距 |
| 文件夹 | 46 | 16 | 12 px 常规、中性色 | 透明，选中加深 + 左侧强调色条 |
| 会话 | 58 | 34 | 12 px 常规、正文色，选中加粗 | 透明，选中加深 |

- 文件夹与会话行左侧各有一条 1 px 层级引导线（文件夹 x=10，会话 x=28），让从属关系可见。
- 副标题固定 10 px：文件夹显示路径，会话显示预览；会话行右侧显示更新时间。
- 组间靠项目行的分组底色与 8 px 上间距断开，不靠分割线。

## 4.3 行内容规格

三级行均已实现，规格与 `SessionSidebar.qml` 一致：

```text
项目行     36 px    标题（12 px 加粗）+ / × / ··· 管理按钮，组前 8 px 间距
文件夹行   46 px    标题（12 px）+ 路径副标题（10 px）+ 折叠箭头 + 管理按钮
会话行     58 px    标题（12 px）+ 预览副标题（10 px）+ 右侧更新时间
```

- 会话行只显示标题、预览与时间，不做图标堆叠；重命名、删除等操作放在项目行与文件夹行的 `···` 菜单里。
- 未归属项目的会话保留在“未分组”分组下，显示规则与项目相同。
- 悬停只改变底色，不改变行高，避免列表跳动。

不建议长期显示大量图标。

原因：

> Sidebar 的核心职责是导航，不是展示 Session 全部信息。

---

# 5. Conversation 主区域

Conversation 是整个产品的核心区域。

推荐内容最大宽度：

```text
760 ~ 900 px
```

在大窗口中不要让文本铺满整个屏幕。

例如：

```text
┌─────────────────────────────────────────────┐
│                                             │
│        ┌────────────────────────────┐       │
│        │                            │       │
│        │      Conversation          │       │
│        │                            │       │
│        │      max-width 860px       │       │
│        │                            │       │
│        └────────────────────────────┘       │
│                                             │
└─────────────────────────────────────────────┘
```

这样可以避免长文本行过长，降低阅读效率。

---

# 6. Message 设计

## 6.1 用户消息

用户消息可以有轻微背景区分，但不建议使用传统聊天气泡。

推荐：

```text
┌────────────────────────────────────────────┐
│                                            │
│  帮我分析这个 WPF Dispatcher 问题。       │
│                                            │
└────────────────────────────────────────────┘
```

视觉特点：

- 轻背景色
- 小圆角
- 不使用明显描边
- 不显示头像
- 不使用左右对话布局

推荐：

```text
Border Radius: 8 px
Padding: 12 ~ 16 px
```

---

## 6.2 Assistant 消息

Assistant 内容默认不使用大背景容器。

例如：

```text
Assistant

这个异常通常意味着 Dispatcher 当前处于挂起状态。

主要可以从以下几个方向排查：

1. Dispatcher.DisableProcessing()
2. UI Thread 阻塞
3. 嵌套消息循环
4. Shutdown 流程

下面分别说明。
```

Assistant 内容尽量直接融入页面。

这也是整个 UI 最接近 Claude 风格的部分。

---

# 7. Markdown 排版

AI Client 的 Markdown 渲染质量非常重要。

## 7.1 正文字号

推荐：

```text
15 ~ 16 px
```

行高：

```text
1.5 ~ 1.65
```

正文最大行宽：

```text
80 ~ 100 characters
```

## 7.2 标题

```text
H1: 24 px
H2: 20 px
H3: 17 px
```

AI 对话通常不需要过大的标题。

---

# 8. Code Block

代码块是 Pi Desktop 的核心组件之一。

推荐结构：

```text
┌──────────────────────────────────────────────┐
│ C++                                   Copy   │
├──────────────────────────────────────────────┤
│                                              │
│  void Foo()                                  │
│  {                                           │
│      std::cout << "hello";                   │
│  }                                           │
│                                              │
└──────────────────────────────────────────────┘
```

Header：

```text
Language                    Copy
```

可扩展：

```text
Language
Copy
Open File
Apply
Diff
```

但默认只显示：

```text
Language
Copy
```

其他操作在 Hover 时出现。

建议代码字体：

```text
JetBrains Mono
Cascadia Code
Consolas
```

推荐字号：

```text
13 ~ 14 px
```

代码块圆角：

```text
8 px
```

---

# 9. Tool Call 展示

pi 会涉及大量工具调用，因此 Tool Call 不应该和普通正文混在一起。

推荐设计：

```text
┌──────────────────────────────────────────────┐
│ › Read                                       │
│   src/Core/SessionManager.cpp                │
└──────────────────────────────────────────────┘
```

复杂 Tool Call：

```text
┌──────────────────────────────────────────────┐
│ ▼ Read File                                  │
│                                              │
│ src/Core/SessionManager.cpp                  │
│                                              │
│ 1.2 KB · 84 lines                            │
└──────────────────────────────────────────────┘
```

默认折叠。

状态：

```text
Running
Success
Failed
Cancelled
```

不要让 Tool Call 抢占 Conversation 的视觉重点。

展开后的 Input / Output 各自独立滚动：

```text
Input    高 min(180, 内容高 + 14)，最小 48
Output   高 min(260, 内容高 + 14)，最小 48
```

框高随内容增长到上限后不再变高，超出部分用右侧滚动条查看；滚动条只在内容溢出时出现。

---

# 10. Composer 输入区

Composer 固定在 Conversation 底部。

推荐设计：

```text
┌────────────────────────────────────────────────────┐
│                                                    │
│ Ask pi...                                          │
│                                                    │
│ +    Model     Context                     Send    │
└────────────────────────────────────────────────────┘
```

推荐高度：

```text
最小   94 px
增长   随内容行数（contentHeight + 42 + 附件行）
上限   窗口高度 × 0.5
```

高度按行数自动增长，到窗口高度一半后不再变高，超出部分由框内右侧滚动条查看。框宽随窗口变化；附件芯片行与 + / 发送按钮行始终可见，高度不够时先压缩中间滚动区。

Composer 不建议做成普通单行 TextBox。

---

# 11. Composer 功能

左侧：

```text
+
```

用于：

- 添加文件
- 添加目录
- 添加图片
- 添加 Context

中部：

```text
Model
Mode
Context
```

右侧：

```text
Send
Stop
```

Agent 正在运行时：

```text
Send
 ↓
Stop
```

---

# 12. Right Panel

Right Panel 是扩展功能区。

默认：

```text
Closed
```

推荐宽度：

```text
280 ~ 360 px
```

可包含：

```text
Outline
Files
Tools
Context
Session Info
```

---

# 13. Outline

这是 Pi Desktop 相比普通聊天客户端非常值得做的功能。

长对话中：

```text
Outline

User
为什么 Dispatcher 会挂起？

Assistant
Dispatcher 原理

User
DisableProcessing 是什么？

Assistant
DisableProcessing 机制

User
怎么排查？

Assistant
排查方案
```

点击 Outline 项：

```text
ScrollToMessage(messageId)
```

直接跳转到对应 Message。

这可以解决：

> 同一个会话里前面的信息很难重新找到。

---

# 14. Conversation Search

快捷键：

```text
Ctrl + F
```

搜索：

```text
Dispatcher
```

结果：

```text
3 Matches

Message 12
Message 24
Message 31
```

点击结果直接跳转。

搜索目标：

```text
User Message
Assistant Message
Code Block
Tool Call
```

---

# 15. Session Search

全局快捷键：

```text
Ctrl + K
```

打开 Command / Search Panel：

```text
┌──────────────────────────────────────────┐
│ Search sessions...                       │
├──────────────────────────────────────────┤
│ Qt + Pi Desktop                          │
│ Dispatcher Debug                         │
│ Memory Monitor                           │
│ OpenCV Bayer Rotation                    │
└──────────────────────────────────────────┘
```

后续可以扩展为 Command Palette。

---

# 16. Command Palette

推荐：

```text
Ctrl + Shift + P
```

支持：

```text
New Chat
Open Session
Rename Session
Delete Session
Toggle Sidebar
Toggle Right Panel
Export Session
Open Settings
Switch Model
```

这类交互非常适合开发者工具。

---

# 17. 色彩系统

整体采用 Claude 风格的：

```text
Warm Neutral
```

避免纯白 + 纯黑。

## Light Theme

建议背景层级：

```text
Window Background
#F7F6F3

Sidebar
#F1F0ED

Conversation
#FAF9F7

Secondary Surface
#EFEEEA
```

正文：

```text
Primary Text
#2B2A27

Secondary Text
#6F6D68

Disabled Text
#A09E98
```

边框：

```text
#DDDAD4
```

Accent：

建议使用低饱和暖色：

```text
#C15F3C
```

不要大面积使用 Accent。

---

# 18. Dark Theme

推荐：

```text
Window
#1E1E1C

Sidebar
#252522

Surface
#2B2B28

Hover
#33332F
```

正文：

```text
Primary
#ECEAE5

Secondary
#AAA7A0
```

边框：

```text
#3C3B37
```

---

# 19. 边框原则

不要大量使用：

```text
Rectangle Border
```

优先依赖：

```text
Background Difference
Spacing
Typography
```

推荐：

```text
90% spacing/background
10% borders
```

例如 Sidebar 与 Conversation：

不需要明显边框。

可以只使用：

```text
1 px subtle divider
```

---

# 20. 圆角

统一控制圆角，不要每个组件都不同。

推荐：

```text
Small      4 px
Normal     8 px
Large     12 px
```

Button：

```text
6 ~ 8 px
```

Composer：

```text
12 px
```

Dialog：

```text
12 px
```

---

# 21. 间距系统

推荐使用：

```text
4 px Grid
```

标准：

```text
4
8
12
16
20
24
32
40
48
```

例如：

```text
Message Gap       24
Paragraph Gap     12
Sidebar Padding   12
Composer Padding  16
```

---

# 22. Button

按钮分为：

```text
Primary
Secondary
Ghost
Danger
```

AI Desktop Client 中应大量使用 Ghost Button。

例如：

```text
Copy
Retry
Edit
More
```

默认状态几乎不可见。

Hover 后：

```text
Background appears
```

---

# 23. Hover 行为

Claude 风格的重要特征之一：

> 很多操作在需要时才出现。

例如 Assistant Message：

默认：

```text
文本内容
```

Hover：

```text
Copy
Retry
More
```

User Message：

```text
Edit
Copy
```

这样可以降低页面视觉噪声。

---

# 24. Context 信息

建议在 Composer 附近显示：

```text
Context 32%
```

或者：

```text
12k / 64k
```

点击后打开：

```text
Context Panel
```

显示：

```text
Conversation
Files
System Prompt
Tool Results
```

对 pi 这种 Agent Client 很有价值。

---

# 25. Agent 状态

状态显示必须弱化。

例如：

```text
Thinking...
```

或者：

```text
Running tool...
```

不要使用大型 Loading Animation。

推荐：

```text
small spinner + text
```

例如：

```text
○ Thinking...
```

---

# 26. Streaming

Assistant Streaming 时：

```text
文本逐步增加
```

不要每个 Token 都触发复杂布局。

建议：

```text
50 ~ 100 ms
```

批量刷新 UI。

否则 Qt Rich Text / Markdown Layout 可能频繁重新排版。

---

# 27. Scroll 行为

Conversation 默认：

```text
Auto Scroll
```

但只有：

```text
用户当前位于底部附近
```

才自动向下滚动。

如果用户正在查看历史内容：

```text
禁止强制滚到底部
```

此时显示：

```text
↓ Jump to latest
```

这是长对话阅读体验的关键。

嵌套滚动规则：

```text
Composer / Tool Input / Tool Output
  鼠标在框内 → 只滚框内内容
  鼠标在框外 → 滚 Conversation
```

内嵌滚动区统一使用 `WorkspaceScrollBar`（内容溢出时才出现），避免原生滚动条带来的系统主题差异。

---

# 28. Message Anchor

每条 Message 必须有：

```text
messageId
```

例如：

```text
msg_000124
```

用于：

```text
Search
Outline
Jump
Bookmark
Copy Link
```

以后可以支持：

```text
pi://session/{sessionId}/message/{messageId}
```

---

# 29. Bookmark

长对话建议加入 Bookmark。

Hover Message：

```text
☆ Bookmark
```

Right Panel：

```text
Bookmarks

Dispatcher 原理
Memory 分析
最终解决方案
```

对于技术对话非常实用。

---

# 30. Window Title Bar

尽量使用 Custom Title Bar。

设计：

```text
┌───────────────────────────────────────────────┐
│ Pi                      —   □   ×             │
└───────────────────────────────────────────────┘
```

Title Bar 高度：

```text
设计建议 36 ~ 40 px；当前实现为 52 px（40 px 应用图标 + 上下留白）
```

左侧实际放：应用图标、名称、当前工作目录；右侧放模型名、连接状态与设置入口。设计稿里的前进/后退与侧栗开关按钮暂未实现。

---

# 31. 推荐最终布局

最终建议：

```text
┌─────────────────────────────────────────────────────────────────────────┐
│ ☰   Pi                                                     —  □  ×     │
├─────────────────┬─────────────────────────────────────────┬─────────────┤
│                 │                                         │             │
│ + 新建对话      │                                         │ Outline     │
│                 │             Conversation                │             │
│ 🔍 搜索         │                                         │ Files       │
│                 │                                         │             │
│ ▾ 项目 A        │ User                                    │ Context     │
│                 │ ┌─────────────────────────────────────┐ │             │
│   │ 文件夹 1    │ │ 为什么这个函数会阻塞？              │ │ Tools       │
│   │   │ 会话 a   │ └─────────────────────────────────────┘ │             │
│                 │                                         │             │
│   ▸ 文件夹 2    │ Assistant                               │             │
│                 │                                         │             │
│ ▾ 项目 B        │ 从代码来看主要有三个问题……             │             │
│   │ 文件夹 3    │                                         │             │
│                 │ ```cpp                                  │             │
│                 │ void Foo()                              │             │
│                 │ {                                       │             │
│                 │ }                                       │             │
│                 │ ```                                     │             │
│                 │                                         │             │
│                 │ › Read src/Foo.cpp                      │             │
│                 │                                         │             │
│                 ├─────────────────────────────────────────┤             │
│ ⚙ 设置          │ Ask pi...                         Send  │             │
└─────────────────┴─────────────────────────────────────────┴─────────────┘
```

---

# 32. MVP 阶段

第一阶段只实现：

```text
MainWindow
Sidebar
SessionList
ConversationView
UserMessage
AssistantMessage
Markdown
CodeBlock
Composer
Streaming
Session Switch
```

不要一开始实现 Right Panel。

---

# 33. 第二阶段

加入：

```text
Tool Call
Session Search
Conversation Search
Message Anchor
Jump To Message
Context Indicator
```

---

# 34. 第三阶段

加入：

```text
Right Panel
Outline
Bookmarks
Files
Tool History
Context Inspector
```

---

# 35. 第四阶段

加入开发者能力：

```text
Diff View
Apply Patch
Open File
Terminal Output
Git Status
Tool Timeline
Agent Execution Tree
```

此时 Pi Desktop 会从：

```text
Chat Client
```

逐渐发展成：

```text
AI Developer Workspace
```

---

# 36. Qt 组件建议

推荐整体：

```text
Qt 6.11
Qt Widgets
```

或者：

```text
Qt Quick / QML
```

如果主要目标是快速做现代 UI，优先考虑：

```text
QML
```

如果更强调传统桌面控件和 C++ 控制：

```text
Qt Widgets
```

界面模块建议：

```text
MainWindow
 ├─ TitleBar
 ├─ Sidebar
 │   ├─ SearchBox
 │   └─ SessionList
 ├─ ConversationPage
 │   ├─ MessageList
 │   │   ├─ UserMessage
 │   │   ├─ AssistantMessage
 │   │   ├─ CodeBlock
 │   │   └─ ToolCall
 │   └─ Composer
 └─ RightPanel
     ├─ Outline
     ├─ Files
     ├─ Tools
     └─ Context
```

---

# 37. 核心设计原则总结

整个 UI 设计最终遵循以下规则：

```text
内容 > 装饰

排版 > 边框

留白 > 分隔线

Hover 操作 > 常驻按钮

文档阅读 > IM 聊天气泡

信息层级 > 视觉特效

功能密度 > 花哨动画
```

最终目标不是做一个“聊天软件”。

而是做一个：

> 适合长时间阅读、检索、回溯和操作 AI Agent 的桌面开发工具。
