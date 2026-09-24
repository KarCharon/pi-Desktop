# Pi Desktop 技术方案

## 1. 项目目标

本项目计划使用 Qt 6.11 开发一个面向 Pi 的桌面客户端。

整体定位不是重新实现 Pi，也不是重新实现 Agent Runtime，而是为 Pi 提供一个图形化桌面 UI。

核心目标：

- 使用聊天界面展示 Pi 会话。
- 支持长会话浏览。
- 支持历史 Session 管理。
- 支持历史消息全文搜索。
- 支持快速定位到会话中的某条消息。
- 展示 Pi 的 Tool Call、执行状态和输出。
- 保留 Pi 原有的 Agent、Session、Tool、Context 等能力。
- Qt 仅作为 UI 和 Desktop Client 层。
- 后续支持会话分支、多个 Agent 并行等高级功能。

项目整体架构原则：

```text
Qt Desktop
    ↓
Pi RPC
    ↓
Pi Agent Runtime
    ↓
LLM / Tools / Context
```

Qt 不直接连接 OpenAI、Claude 或其他模型。

Qt 只连接 Pi。

---

# 2. 技术栈

## 2.1 基础技术栈

推荐：

```text
Qt 6.11
C++20
Qt Quick / QML
CMake
SQLite
Pi RPC
```

主要组件：

| 模块 | 技术 |
|---|---|
| UI | Qt Quick / QML |
| Backend | C++20 |
| Pi 进程管理 | QProcess |
| Pi 通信协议 | JSONL |
| JSON | QJsonDocument / QJsonObject |
| Chat 数据模型 | QAbstractListModel |
| Session 数据模型 | QAbstractListModel |
| Markdown | QTextDocument / QML TextEdit |
| 代码高亮 | QSyntaxHighlighter |
| 搜索 | SQLite FTS5 |
| 配置 | QSettings |
| 构建系统 | CMake |

---

# 3. 为什么选择 Qt Quick / QML

不建议主要使用 Qt Widgets。

本项目 UI 类型更接近：

```text
ChatGPT Desktop
Claude Desktop
Claude Code Desktop
Cursor
IDE Chat Panel
```

这种界面的特点：

- 大量动态内容。
- 长列表。
- 消息流。
- 卡片。
- Tool Call 展开/折叠。
- Streaming 更新。
- 动态高度。
- 动画。
- 主题切换。

Qt Quick / QML 比 Qt Widgets 更适合这种场景。

推荐架构：

```text
QML
↓
C++ ViewModel / Model
↓
Pi RPC
```

QML 只负责：

```text
布局
样式
动画
交互
```

C++ 负责：

```text
进程管理
RPC
JSON
Session
Model
SQLite
业务状态
```

---

# 4. 总体架构

```text
┌──────────────────────────────────────────────┐
│                 Pi Desktop                   │
│                                              │
│ ┌───────────────┐ ┌───────────────────────┐ │
│ │ Session Panel │ │       Chat View       │ │
│ │               │ │                       │ │
│ │ Project A     │ │ User Message          │ │
│ │   Session 1   │ │                       │ │
│ │   Session 2   │ │ Assistant Message     │ │
│ │               │ │                       │ │
│ │ Project B     │ │ Tool Call             │ │
│ │   Session 3   │ │                       │ │
│ │               │ │ Assistant Message     │ │
│ └───────────────┘ └───────────────────────┘ │
│                                              │
│                    ┌───────────────────────┐ │
│                    │    Prompt Editor      │ │
│                    └───────────────────────┘ │
│                                              │
└──────────────────────┬───────────────────────┘
                       │
                       │ JSONL
                       │ stdin/stdout
                       ▼
                ┌───────────────┐
                │   QProcess    │
                └───────┬───────┘
                        │
                        ▼
                pi --mode rpc
                        │
                        ▼
                ┌───────────────┐
                │ Pi Agent      │
                │ Runtime       │
                │               │
                │ Model         │
                │ Tools         │
                │ Session       │
                │ Context       │
                └───────────────┘
```

---

# 5. 架构边界

整个项目最重要的是控制职责边界。

## 5.1 Qt 负责

Qt Desktop 负责：

```text
UI
消息展示
Session 展示
历史搜索
消息定位
Pi 进程启动
RPC 通信
本地搜索索引
桌面配置
```

---

## 5.2 Pi 负责

Pi 继续负责：

```text
Agent Runtime
LLM API
模型切换
Prompt
Context
Tool Call
Tool Execution
Session
Compaction
Branch
Fork
模型输出 Streaming
```

不建议在 Qt 中重新实现这些功能。

---

# 6. Pi 接入方式

推荐直接使用 Pi RPC：

```bash
pi --mode rpc
```

Qt 使用：

```cpp
QProcess
```

启动 Pi。

例如：

```cpp
QProcess piProcess;

piProcess.start(
    "pi",
    {
        "--mode",
        "rpc"
    }
);
```

随后通过：

```text
stdin
stdout
```

交换 JSONL 数据。

---

# 7. 为什么不要解析 Pi Terminal

不推荐：

```text
Qt
 ↓
QProcess
 ↓
Pi Terminal UI
 ↓
ANSI Escape Sequence
 ↓
解析终端文本
```

这种方案的问题：

- 需要处理 ANSI Escape Sequence。
- 终端 UI 本身有自己的状态。
- 很难准确识别消息。
- Tool Call 不容易结构化。
- Streaming 处理麻烦。
- Session 控制麻烦。

应该直接使用：

```text
Pi RPC
```

流程：

```text
Qt
 │
 │ JSON
 ▼
Pi RPC
 │
 │ Event
 ▼
Qt
```

---

# 8. RPC 通信结构

Pi RPC 使用 JSONL。

即：

```text
一个 JSON 对象
+
一个换行符
```

例如：

```json
{"type":"prompt","message":"分析一下这个项目"}
```

然后 Pi 返回事件：

```text
message_start

message_update

message_update

tool_execution_start

tool_execution_update

tool_execution_end

message_end

agent_settled
```

Qt 只需要：

```text
读取 stdout
    ↓
按 \n 分割
    ↓
解析 JSON
    ↓
转换为 PiEvent
```

---

# 9. C++ Backend 分层

不要让 `QProcess` 直接操作 Chat UI。

推荐：

```text
QML
 │
 ▼
AppController
 │
 ▼
AgentSessionController
 │
 ▼
PiRpcClient
 │
 ▼
PiProcess
 │
 ▼
Pi
```

各模块职责如下。

---

# 10. PiProcess

职责：

```text
启动 Pi
关闭 Pi
监控进程
stdin 写入
stdout 读取
stderr 读取
进程崩溃检测
自动重启
```

接口示例：

```cpp
class PiProcess : public QObject
{
    Q_OBJECT

public:

    bool start();

    void stop();

    void write(const QByteArray& data);

signals:

    void outputReceived(QByteArray data);

    void errorReceived(QByteArray data);

    void processExited(int exitCode);

private:

    QProcess m_process;
};
```

PiProcess 不理解：

```text
prompt
message
tool
session
```

只处理进程。

---

# 11. PiRpcClient

PiRpcClient 负责 RPC 协议。

职责：

```text
JSON 编码
JSON 解码
JSONL framing
RPC request id
RPC response correlation
Event dispatch
```

结构：

```text
QProcess stdout
      ↓
PiRpcClient
      ↓
PiEvent
```

例如：

```cpp
class PiRpcClient : public QObject
{
    Q_OBJECT

public:

    void sendPrompt(const QString& prompt);

    void abort();

    void switchSession(const QString& path);

signals:

    void eventReceived(const PiEvent& event);

private:

    QByteArray m_buffer;
};
```

---

# 12. AgentSessionController

这是 Desktop 中比较核心的一层。

负责：

```text
当前 Session
Prompt
Abort
Fork
Steer
FollowUp
Session 切换
Agent 状态
```

架构：

```text
PiRpcClient
      ↓
AgentSessionController
      ↓
ChatModel
SessionModel
```

例如：

```cpp
class AgentSessionController : public QObject
{
    Q_OBJECT

public:

    void prompt(const QString& text);

    void abort();

    void switchSession(const QString& sessionPath);

private:

    PiRpcClient* m_rpcClient;

    ChatModel* m_chatModel;

    SessionModel* m_sessionModel;
};
```

---

# 13. ChatModel

聊天消息应该使用：

```cpp
QAbstractListModel
```

不要直接从 C++ 创建 QML Item。

例如：

```cpp
struct ChatMessage
{
    QString id;

    MessageRole role;

    QString content;

    MessageState state;

    QList<ToolCall> tools;
};
```

其中：

```cpp
enum class MessageRole
{
    User,

    Assistant,

    System,

    Tool
};
```

状态：

```cpp
enum class MessageState
{
    Pending,

    Streaming,

    Completed,

    Failed
};
```

---

# 14. QML ChatView

QML 使用：

```qml
ListView
```

例如：

```text
ChatModel
   ↓
ListView
   ↓
Delegate
```

不同消息对应不同 Delegate：

```text
UserMessage.qml

AssistantMessage.qml

ToolCall.qml

SystemMessage.qml
```

整体：

```text
ChatView.qml

 ├── UserMessage
 ├── AssistantMessage
 ├── ToolCall
 ├── AssistantMessage
 ├── UserMessage
 └── AssistantMessage
```

---

# 15. 为什么使用 ListView

不要：

```qml
Column {
    Message {}
    Message {}
    Message {}
    Message {}
}
```

因为长 Session 很容易出现：

```text
大量 QML Item
大量 Text
大量 Layout
大量内存
```

推荐：

```qml
ListView
```

原因：

```text
Delegate Virtualization
```

只创建当前可视区域附近的消息组件。

对于：

```text
1000 Message

5000 Message
```

这种情况会明显更合适。

---

# 16. Streaming 消息处理

Pi 输出通常不是一次完整返回。

例如：

```text
Assistant:

正在
分析
你的
代码
...
```

Qt 需要维护：

```text
一个正在 Streaming 的 ChatMessage
```

流程：

```text
message_start
      ↓
ChatModel::appendMessage()

message_update
      ↓
ChatModel::appendContent()

message_update
      ↓
ChatModel::appendContent()

message_end
      ↓
ChatModel::completeMessage()
```

不要每收到 Token 就重新创建整个消息。

只更新：

```text
content role
```

并发送：

```cpp
dataChanged()
```

---

# 17. Tool Call UI

Agent Desktop 不能只显示聊天文本。

Tool Call 本身应该成为独立 UI 类型。

例如：

```text
Assistant

我检查一下项目文件。

┌─────────────────────────────┐
│ Read File                   │
│ src/main.cpp                │
│                             │
│ ✓ Completed                 │
└─────────────────────────────┘

代码问题在这里……
```

Tool Call 数据：

```cpp
struct ToolCall
{
    QString id;

    QString toolName;

    QString input;

    QString output;

    ToolState state;
};
```

状态：

```cpp
enum class ToolState
{
    Pending,

    Running,

    Completed,

    Failed
};
```

---

# 18. Tool Call 展开/折叠

默认建议：

```text
Tool Call
```

采用折叠形式。

例如：

```text
▼ read_file

src/app/main.cpp

Completed
```

展开：

```text
▼ read_file

Input:

{
    "path": "src/app/main.cpp"
}

Output:

...
```

这样可以避免聊天界面被 Tool Output 完全占据。

展开区的实现约定：

- Input / Output 用 `TextArea` 包 `ScrollView`。`TextArea` 不是 `Flickable`（`QQuickTextEdit` 的基类是 `QQuickImplicitSizeItem`），直接挂 `ScrollBar` 会被判定非法，也不会真正滚动。
- 框高绑定 `min(上限, max(48, contentHeight + 14))`：Input 上限 180、Output 上限 260，超过上限由滚动条查看。
- 覆盖 `ScrollView` 自带的滚动条时必须自行补 `parent` / `x` / `y` / `height` 绑定（样式里就是手写的），否则滑条会落在左上角且拖不动。
- 同一个坑也适用于右侧 Context Panel；Conversation 与会话栗用的是 `ListView`（`Flickable`），附加滚动条由 `Flickable` 自动定位，不需要手写绑定。

---

# 19. Markdown 渲染

第一版不建议直接使用 Qt WebEngine。

推荐：

```text
QTextDocument
+
QML TextEdit
```

例如：

```qml
TextEdit {
    textFormat: TextEdit.MarkdownText

    readOnly: true
}
```

这样可以支持：

```text
标题
列表
代码块
表格
引用
粗体
链接
```

---

# 20. 代码高亮

代码块可以后续增加：

```cpp
QSyntaxHighlighter
```

支持：

```text
C++
C#
Python
Rust
JSON
XML
Shell
CMake
```

第一阶段甚至可以先不做复杂高亮。

---

# 21. 为什么暂时不用 WebEngine

WebEngine 的优势：

```text
完整 HTML
CSS
JavaScript
Mermaid
MathJax
复杂 Markdown
```

但是代价：

```text
Chromium
包体积大
内存高
架构复杂
C++/JS 通信
```

对于第一版 Pi Desktop：

```text
没有必要。
```

后续如果需要：

```text
Mermaid
LaTeX
复杂表格
Diff Viewer
Interactive HTML
```

再考虑。

---

# 22. Session 管理

左侧应该设计一个 Session Sidebar。

层级实际实现为三级：项目 → 工作文件夹 → 会话（未归入项目的会话放在“未分组”）。

例如：

```text
项目 A                  （分组标题：36 px、加粗、分组底色、组前 8 px 间距）
    └ 工作文件夹 1       （46 px、缩进 16 px、显示路径、左侧引导线）
        └ 会话 1         （58 px、缩进 34 px、显示预览与更新时间、左侧引导线）
        └ 会话 2
    └ 工作文件夹 2
项目 B
    └ 工作文件夹 3
        └ 调试内存泄漏
```

层级区分手段：行高、缩进、标题字重与色调、左侧 1 px 引导线；当前工作文件夹额外用左侧 3 px 强调色条标记。搜索命中时只显示匹配行及其祖先。

对应：

```cpp
SessionModel : public QAbstractListModel
```

Session 数据：

```cpp
struct SessionInfo
{
    QString id;

    QString name;

    QString path;

    QString projectPath;

    QDateTime createTime;

    QDateTime updateTime;
};
```

---

# 23. Pi Session 是数据源

不要自己重新设计一套 Session 格式。

应该：

```text
Pi Session
=
Source Of Truth
```

Qt 只是：

```text
展示
索引
搜索
缓存
```

不要出现：

```text
Qt Session DB
        +
Pi Session DB
```

两套真实状态。

否则会产生同步问题。

---

# 24. SQLite 的定位

SQLite 不保存 Agent 的真实状态。

SQLite 主要用于：

```text
搜索索引
Session Metadata Cache
UI Metadata
Favorite
Tag
Pin
```

例如：

```text
Pi Session JSONL
       ↓
SessionIndexer
       ↓
SQLite FTS5
```

SQLite 是：

```text
Search Cache
```

不是：

```text
Session Source
```

---

# 25. SQLite 表设计

建议：

```sql
CREATE TABLE session
(
    id TEXT PRIMARY KEY,

    path TEXT,

    project_path TEXT,

    name TEXT,

    created_at INTEGER,

    updated_at INTEGER
);
```

消息索引：

```sql
CREATE VIRTUAL TABLE message_fts
USING fts5
(
    session_id,

    entry_id,

    role,

    content,

    timestamp
);
```

---

# 26. 历史搜索

这应该作为 Pi Desktop 的核心特色。

例如顶部：

```text
Search Messages...
```

搜索：

```text
ThreadLocalBlock
```

结果：

```text
Project AOI

Session: CLR Crash

14:32 User
ThreadLocalBlock::FreeTable 是做什么的？

14:35 Assistant
ThreadLocalBlock 主要负责……
```

点击：

```text
Search Result
      ↓
Session
      ↓
Entry ID
      ↓
ChatModel
      ↓
ListView.positionViewAtIndex()
```

直接定位。

---

# 27. 推荐 UI 布局

可以采用三栏结构：

```text
┌───────────────┬────────────────────────────┬──────────────┐
│ Session       │          Chat              │ Context      │
│               │                            │              │
│ Project A     │ User                       │ Outline      │
│               │                            │              │
│ Session 1     │ Assistant                  │ Search       │
│ Session 2     │                            │              │
│ Session 3     │ Tool Call                  │ Files        │
│               │                            │              │
│ Project B     │ Assistant                  │ Branch       │
│               │                            │              │
└───────────────┴────────────────────────────┴──────────────┘
```

其中右侧面板第一阶段可以暂时不做。

---

# 28. 第一版推荐布局

第一版保持简单：

```text
┌───────────────┬────────────────────────────┐
│ Session       │ Chat                       │
│               │                            │
│               │                            │
│               │                            │
│               │                            │
│               │                            │
│               ├────────────────────────────┤
│               │ Prompt Editor              │
└───────────────┴────────────────────────────┘
```

顶部：

```text
Project

Model

Session Name

Search

Settings
```

---

# 29. Conversation Outline

后期可以做一个非常有价值的功能：

```text
Conversation Outline
```

自动把长对话整理成：

```text
1. 问题背景

2. ThreadLocalBlock 分析

3. Dump 分析

4. Root Cause

5. 修复方案

6. 最终代码
```

点击标题直接定位到对应消息。

这样比 Terminal 不断往上翻体验好很多。

---

# 30. Session Tree

Pi Session 后续可以利用 Tree 结构。

例如：

```text
                ┌── Branch A
User
  │
Assistant
  │
User
  │
Assistant
  │
  └─────────────┬── Branch B
                │
                └── Branch C
```

UI 可以做成类似：

```text
Git Branch
```

或者：

```text
Conversation Tree
```

第一版不需要。

---

# 31. 多 Agent

第一版建议：

```text
1 Desktop

1 Pi Process

1 Active Session
```

切换 Session：

```text
switch_session
```

不要第一版直接设计：

```text
Session A → Pi Process A

Session B → Pi Process B

Session C → Pi Process C
```

复杂度会上升很多。

---

# 32. 后续多 Agent 架构

需要并行 Agent 时：

```text
PiProcessManager
       │
       ├── AgentInstance A
       │       │
       │       └── PiProcess A
       │
       ├── AgentInstance B
       │       │
       │       └── PiProcess B
       │
       └── AgentInstance C
               │
               └── PiProcess C
```

这样可以：

```text
Agent A
分析项目

Agent B
修改代码

Agent C
跑测试
```

但是不建议一开始实现。

---

# 33. 推荐目录结构

```text
PiDesktop/

├── CMakeLists.txt
│
├── src/
│
│   ├── main.cpp
│
│   ├── app/
│   │
│   │   ├── AppController.h
│   │   └── AppController.cpp
│   │
│   ├── pi/
│   │
│   │   ├── PiProcess.h
│   │   ├── PiProcess.cpp
│   │   │
│   │   ├── PiRpcClient.h
│   │   ├── PiRpcClient.cpp
│   │   │
│   │   ├── PiEvent.h
│   │   └── PiEvent.cpp
│   │
│   ├── agent/
│   │
│   │   ├── AgentSessionController.h
│   │   └── AgentSessionController.cpp
│   │
│   ├── chat/
│   │
│   │   ├── ChatMessage.h
│   │   ├── ChatModel.h
│   │   ├── ChatModel.cpp
│   │   │
│   │   ├── ToolCall.h
│   │   └── ToolCall.cpp
│   │
│   ├── session/
│   │
│   │   ├── SessionInfo.h
│   │   ├── SessionModel.h
│   │   ├── SessionModel.cpp
│   │   │
│   │   ├── SessionIndexer.h
│   │   └── SessionIndexer.cpp
│   │
│   ├── search/
│   │
│   │   ├── SearchEngine.h
│   │   ├── SearchEngine.cpp
│   │   │
│   │   ├── SearchModel.h
│   │   └── SearchModel.cpp
│   │
│   └── config/
│
│       ├── AppSettings.h
│       └── AppSettings.cpp
│
├── qml/
│
│   ├── Main.qml
│
│   ├── pages/
│   │
│   │   ├── ChatPage.qml
│   │   └── SettingsPage.qml
│   │
│   └── components/
│
│       ├── ChatView.qml
│       ├── UserMessage.qml
│       ├── AssistantMessage.qml
│       ├── ToolCall.qml
│       ├── CodeBlock.qml
│       ├── PromptEditor.qml
│       ├── SessionSidebar.qml
│       └── SearchPanel.qml
│
├── database/
│
│   └── schema.sql
│
└── resources/
```

---

# 34. 数据流

用户发送 Prompt：

```text
PromptEditor
      ↓
AppController
      ↓
AgentSessionController
      ↓
PiRpcClient
      ↓
PiProcess
      ↓
stdin
      ↓
Pi
```

Pi 返回：

```text
Pi
 ↓
stdout
 ↓
PiProcess
 ↓
PiRpcClient
 ↓
PiEvent
 ↓
AgentSessionController
 ↓
ChatModel
 ↓
QML ListView
```

---

# 35. Signal / Slot

Qt 本身非常适合 Pi RPC 的事件流。

例如：

```text
PiProcess

outputReceived()
      ↓
PiRpcClient

eventReceived()
      ↓
AgentSessionController

messageUpdated()
      ↓
ChatModel

dataChanged()
      ↓
QML
```

这套结构很自然。

---

# 36. Thread 模型

第一版不要过度设计线程。

可以：

```text
UI Thread

    QML

    Models

    QProcess

    RPC
```

因为：

```text
QProcess
```

本身就是异步事件驱动的。

不需要专门为了 Pi RPC 创建线程。

真正需要后台线程的主要是：

```text
Session 全量扫描

SQLite Index

大型文件解析

全文索引更新
```

可以后续通过：

```text
QThreadPool

QtConcurrent
```

处理。

---

# 37. 不建议第一版实现的功能

第一版先不要做：

```text
多 Agent

WebEngine

插件系统

远程 Pi

TCP RPC

自定义 Tool Runtime

模型 API

复杂 Session Tree

Mermaid

LaTeX

Diff Editor

Git Integration

Code Editor
```

这些都可以后续加入。

---

# 38. 第一阶段

目标：

```text
Qt 可以正常连接 Pi。
```

实现：

```text
QProcess

Pi RPC

JSONL

Prompt

Streaming

Abort
```

UI：

```text
一个聊天窗口

一个输入框

一个发送按钮
```

完成后：

```text
Qt
↓
Pi
↓
Model
↓
Qt
```

整个链路跑通。

---

# 39. 第二阶段

实现基础 Desktop Client：

```text
ChatModel

QAbstractListModel

User Message

Assistant Message

Streaming

Markdown

自动滚动
```

达到：

```text
基本可以替代 Terminal 聊天。
```

---

# 40. 第三阶段

实现 Tool Call：

```text
Tool Call Card

Tool Status

Tool Input

Tool Output

Collapse / Expand
```

此时基本具备：

```text
Claude Code Desktop
```

类似的交互形式。

---

# 41. 第四阶段

实现 Session：

```text
Session Sidebar

New Session

Switch Session

Session Name

Recent Sessions
```

Qt Desktop 从：

```text
Chat Client
```

升级为：

```text
Pi Desktop Client
```

---

# 42. 第五阶段

实现全文搜索：

```text
SQLite

FTS5

Session Indexer

Search Panel

Message Jump
```

这是整个项目非常关键的一阶段。

目标：

```text
快速找到几十轮以前的内容。
```

---

# 43. 第六阶段

增加长会话增强能力：

```text
Conversation Outline

Bookmark

Pin Message

Tag

Favorite

Search Filter
```

例如：

```text
只搜索：

User Message

Assistant Message

Tool Result

Code
```

---

# 44. 第七阶段

增加高级 Session 能力：

```text
Fork

Branch

Tree

Conversation Graph
```

利用 Pi 本身的 Session Tree。

---

# 45. 第八阶段

如果确实需要，再加入：

```text
Multi Agent

Multi Pi Process

Agent Dashboard
```

例如：

```text
Agent A

Running

Agent B

Waiting Tool

Agent C

Completed
```

---

# 46. 开发优先级

推荐顺序：

```text
1. PiProcess

2. PiRpcClient

3. ChatModel

4. QML ChatView

5. Streaming

6. Tool Call

7. Session

8. SQLite Search

9. Conversation Outline

10. Branch

11. Multi Agent
```

不要先做 UI 外观。

首先确认：

```text
Pi RPC
```

整个通信链路完全可用。

---

# 47. 第一版 MVP

MVP 建议只包括：

```text
启动 Pi

发送 Prompt

显示 User Message

显示 Assistant Message

Streaming

Abort

Tool Call

历史 Session

切换 Session

Markdown
```

暂时不做：

```text
全文搜索

Branch

多 Agent

Conversation Outline
```

这样 MVP 比较容易控制。

---

# 48. 第二版重点

第二版主要解决你的核心痛点：

> 长对话不好找历史信息。

增加：

```text
SQLite FTS5

Session Search

Message Search

Message Jump

Bookmark

Conversation Outline
```

这部分会真正体现 GUI 相比 Pi Terminal 的价值。

---

# 49. 设计原则

整个项目建议始终遵守几个原则。

## Qt 不重新实现 Agent

```text
Pi = Agent Runtime

Qt = Desktop Client
```

---

## Pi Session 是唯一事实源

```text
Pi Session

Source Of Truth
```

SQLite 只是：

```text
Cache

Index
```

---

## C++ 不直接操作 QML 组件

应该：

```text
C++

Model
      ↓
QML
```

而不是：

```text
C++

find QML Item

setProperty()
```

---

## RPC 与 UI 解耦

应该：

```text
QProcess

↓

PiRpcClient

↓

Controller

↓

Model

↓

QML
```

而不是：

```text
QProcess

↓

ChatWindow
```

---

# 50. 最终技术方案

最终推荐技术栈：

```text
Qt 6.11

C++20

Qt Quick / QML

QAbstractListModel

QProcess

Pi RPC

JSONL

QJsonDocument

QTextDocument

QSyntaxHighlighter

SQLite

FTS5

QSettings

CMake
```

整体架构：

```text
                 QML UI
                    │
                    ▼
              AppController
                    │
                    ▼
        AgentSessionController
                    │
             ┌──────┴──────┐
             ▼             ▼
        ChatModel      SessionModel
             │
             └──────┬──────┘
                    ▼
               PiRpcClient
                    │
                    ▼
                PiProcess
                    │
                    ▼
               stdin/stdout
                    │
                    ▼
             pi --mode rpc
                    │
                    ▼
              Pi Agent Runtime
```

项目本质可以定义为：

> 一个使用 Qt/C++ 实现的 Pi Desktop Client，通过 Pi RPC 将 Agent 会话、Tool Call 和 Session 以现代聊天界面的形式呈现，并重点增强长会话浏览、历史搜索和信息定位能力。

从工程实现角度，第一阶段真正需要重点掌握的 Qt 技术并不多：

```text
Qt Quick / QML

QAbstractListModel

Signal / Slot

QProcess

QJsonDocument

QSettings

SQLite
```

这些本身也基本覆盖了 C++/Qt 桌面程序比较核心的一套开发模式。