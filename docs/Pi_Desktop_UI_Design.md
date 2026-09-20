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

```text
┌────────────────────────┐
│ + New Chat             │
│                        │
│ 🔍 Search              │
├────────────────────────┤
│ Today                  │
│                        │
│ Qt + Pi Desktop        │
│ WPF Dispatcher         │
│ C++ Memory Monitor     │
│                        │
│ Yesterday              │
│                        │
│ Modbus Debug           │
│ OpenCV Bayer           │
│                        │
│ Earlier                │
│ ...                    │
├────────────────────────┤
│ Settings               │
└────────────────────────┘
```

## 4.3 Session Item

每个 Session Item 推荐高度：

```text
36 ~ 42 px
```

默认只显示：

```text
Session Title
```

悬停后显示：

```text
...
```

用于：

- Rename
- Pin
- Delete
- Export
- Copy Session ID

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
56 ~ 160 px
```

自动随输入内容增长。

最大高度之后内部滚动。

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
36 ~ 40 px
```

可以把：

```text
Sidebar Toggle
Back
Forward
```

放在左侧。

---

# 31. 推荐最终布局

最终建议：

```text
┌─────────────────────────────────────────────────────────────────────────┐
│ ☰   Pi                                                     —  □  ×     │
├─────────────────┬─────────────────────────────────────────┬─────────────┤
│                 │                                         │             │
│ + New Chat      │                                         │ Outline     │
│                 │             Conversation                │             │
│ Search          │                                         │ Files       │
│                 │                                         │             │
│ Today           │ User                                    │ Context     │
│                 │ ┌─────────────────────────────────────┐ │             │
│ Qt + Pi         │ │ 为什么这个函数会阻塞？              │ │ Tools       │
│ WPF Dispatcher  │ └─────────────────────────────────────┘ │             │
│                 │                                         │             │
│ Yesterday       │ Assistant                               │             │
│                 │                                         │             │
│ C++ Memory      │ 从代码来看主要有三个问题……             │             │
│ OpenCV Bayer    │                                         │             │
│                 │ ```cpp                                  │             │
│                 │ void Foo()                              │             │
│                 │ {                                       │             │
│                 │ }                                       │             │
│                 │ ```                                     │             │
│                 │                                         │             │
│                 │ › Read src/Foo.cpp                      │             │
│                 │                                         │             │
│                 ├─────────────────────────────────────────┤             │
│ Settings        │ Ask pi...                         Send  │             │
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
