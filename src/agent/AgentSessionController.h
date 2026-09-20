#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>

class ChatModel;
class PiProcess;
class PiRpcClient;
class PiEvent;

/**
 * 协调当前 Pi Session、Agent 状态、RPC 事件与聊天模型。
 */
class AgentSessionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString queueText READ queueText NOTIFY queueChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString sessionName READ sessionName NOTIFY sessionChanged)
    Q_PROPERTY(QString sessionFile READ sessionFile NOTIFY sessionChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString thinkingLevel READ thinkingLevel NOTIFY thinkingLevelChanged)
    Q_PROPERTY(QString workingText READ workingText NOTIFY workingTextChanged)
    Q_PROPERTY(QVariantMap sessionStats READ sessionStats NOTIFY sessionStatsChanged)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)

public:
    /**
     * 创建当前 Agent Session 的业务控制器。
     */
    AgentSessionController(PiProcess *process, PiRpcClient *rpcClient,
                           ChatModel *chatModel, QObject *parent = nullptr);

    /** 返回 Agent 是否正在处理。 */
    [[nodiscard]] bool busy() const;
    /** 返回 Pi 子进程是否已连接。 */
    [[nodiscard]] bool connected() const;
    /** 返回用于 UI 展示的运行状态。 */
    [[nodiscard]] QString statusText() const;
    /** 返回当前 Session 名称。 */
    [[nodiscard]] QString sessionName() const;
    /** 返回当前 Session 文件。 */
    [[nodiscard]] QString sessionFile() const;
    /** 返回当前模型名称。 */
    [[nodiscard]] QString modelName() const;
    /** 返回当前思考等级，未获取时返回空字符串。 */
    [[nodiscard]] QString thinkingLevel() const;
    /** 返回当前随机英文工作提示，忙碌期间每十秒切换。 */
    [[nodiscard]] QString workingText() const;
    /** 返回 Pi 提供的会话累计统计与当前上下文估计。 */
    [[nodiscard]] QVariantMap sessionStats() const;
    /** 返回 Pi 上报的可用命令、提示模板与技能。 */
    [[nodiscard]] QVariantList commands() const;
    /** 返回当前待随下一条 Prompt 发送的附件。 */
    [[nodiscard]] QVariantList attachments() const;

    /**
     * 提交用户 Prompt；忙碌时按引导或后续策略排队，返回是否成功写入管道。
     */
    Q_INVOKABLE bool prompt(const QString &text, bool followUp = false);

    /** 返回 Pi 权威队列摘要。 */
    QString queueText() const;
    /** 取回 Pi 队列文本，供编辑器继续编辑。 */
    Q_INVOKABLE void retrieveQueue();

    /**
     * 中止当前 Agent 操作。
     */
    Q_INVOKABLE void abort();

    /**
     * 创建新 Session。
     */
    Q_INVOKABLE void newSession();

    /**
     * 切换到历史 Session。
     */
    Q_INVOKABLE void switchSession(const QString &path);

    /** 在重启切换目录前关闭提交入口并清除旧会话状态。 */
    void prepareWorkspaceSwitch();

    /**
     * 向 Pi 回复扩展 UI 对话。
     */
    Q_INVOKABLE void respondToExtension(const QString &id, const QVariantMap &result);

    /**
     * 添加文件或图片附件；接受本地路径或 file URL 列表。
     */
    Q_INVOKABLE bool attachFiles(const QVariantList &fileUrls);

    /**
     * 移除指定下标的待发送附件。
     */
    Q_INVOKABLE void removeAttachment(int index);

    /**
     * 清空全部待发送附件。
     */
    Q_INVOKABLE void clearAttachments();

    /**
     * 切换到指定模型，供 /model 选择弹窗回调用。
     */
    Q_INVOKABLE void selectModel(const QString &provider, const QString &modelId);

    /**
     * 设置思考等级，供 /thinking 选择弹窗回调用。
     */
    Q_INVOKABLE void selectThinkingLevel(const QString &level);

signals:
    /** 待发送队列变化。 */
    void queueChanged();
    /** 将取回或发送失败的文本合并到现有草稿。 */
    void restoreDraftRequested(const QString &text);
    /** Agent 忙碌状态变化。 */
    void busyChanged();
    /** Pi 连接状态变化。 */
    void connectedChanged();
    /** 状态栏文本变化。 */
    void statusTextChanged();
    /** 当前 Session 信息变化。 */
    void sessionChanged();
    /** 当前模型名称变化。 */
    void modelNameChanged();
    /** 当前思考等级变化。 */
    void thinkingLevelChanged();
    /** 英文工作提示变化。 */
    void workingTextChanged();
    /** 上下文及累计统计变化。 */
    void sessionStatsChanged();
    /** 可用命令列表变化。 */
    void commandsChanged();
    /** 待发送附件列表变化。 */
    void attachmentsChanged();
    /** Pi 确认新会话创建成功。 */
    void newSessionCreated();
    /** Session 内容或列表可能已变化。 */
    void sessionsChanged();
    /** /model 请求展示模型选择列表。 */
    void modelSelectionRequested(const QVariantList &models, const QString &current);
    /** /thinking 请求展示思考等级选择列表。 */
    void thinkingSelectionRequested(const QVariantList &levels, const QString &current);
    /** 扩展请求显示交互对话。 */
    void extensionDialogRequested(const QVariantMap &request);
    /** 扩展请求修改输入编辑器文本。 */
    void editorTextRequested(const QString &text);

private:
    /**
     * 分派单条 Pi RPC 事件。
     */
    void handleEvent(const PiEvent &event);

    /**
     * 处理 RPC command response。
     */
    void handleResponse(const QJsonObject &payload);

    /**
     * 处理扩展 UI 子协议请求。
     */
    void handleExtensionUi(const QJsonObject &payload);

    /**
     * 更新忙碌状态并发出必要信号。
     */
    void setBusy(bool busy);

    /** 随机选择不同的英文提示，仅替换普通思考状态，不覆盖工具或错误状态。 */
    void rotateWorkingText();

    /**
     * 更新状态栏文本。
     */
    void setStatusText(const QString &status);

    /**
     * 从 Pi 内容块提取文本。
     */
    [[nodiscard]] static QString extractText(const QJsonValue &content);

    /** 请求一次统计并记录编号，忽略跨会话的旧响应。 */
    void refreshSessionStats();
    /** 请求一次可用命令列表。 */
    void refreshCommands();
    /** 拦截并执行桌面端内置斜杠命令，返回是否已处理。 */
    bool handleBuiltinCommand(const QString &text);
    /** 返回桌面端内置命令列表，用于与 Pi 命令合并补全。 */
    [[nodiscard]] static QVariantList builtinCommands();
    /** 将附件合并进 Prompt 文本与 images 字段。 */
    void buildPromptPayload(const QString &text, QString &message, QJsonArray &images) const;
    /** 批量展示有界 stderr，不将普通诊断一律标记为致命错误。 */
    void flushDiagnostics();
    /** 展示运行错误并保留本轮故障标记。 */
    void reportRuntimeError(const QString &message);

    PiProcess *m_process;
    PiRpcClient *m_rpcClient;
    ChatModel *m_chatModel;
    bool m_busy = false;
    bool m_ready = false;
    QString m_initialStateRequest;
    QString m_initialMessagesRequest;
    QString m_queueText;
    QHash<QString, QPair<QString, bool>> m_promptRequests;
    QString m_statusText;
    QString m_sessionName;
    QString m_sessionFile;
    QString m_modelName;
    QString m_thinkingLevel;
    QString m_currentProvider;
    QString m_currentModelId;
    QString m_pendingThinkingLevel;
    QString m_pendingSessionName;
    QString m_workingText = QStringLiteral("Thinking");
    QVariantMap m_sessionStats;
    QVariantList m_commands;
    QVariantList m_attachments;
    QString m_statsRequestId;
    bool m_statsSupported = true;
    bool m_runHadError = false;
    QTimer m_diagnosticTimer;
    QTimer m_workingTimer;
    QByteArray m_diagnostics;
    QElapsedTimer m_promptElapsed;
    qint64 m_firstEventMs = -1;
    qint64 m_firstTextMs = -1;
};
