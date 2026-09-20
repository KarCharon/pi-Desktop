#include "AgentSessionController.h"

#include "chat/ChatModel.h"
#include "pi/PiEvent.h"
#include "pi/PiProcess.h"
#include "pi/PiRpcClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSet>
#include <QUrl>

namespace {
/** 单条附件上限，避免一次 Prompt 撑爆 64 MiB 的 JSONL 分帧上限。 */
constexpr qint64 kMaximumAttachmentBytes = 10 * 1024 * 1024;

/**
 * 按内容嗅探 Pi 支持的图片类型，非图片返回空。
 */
QString detectImageMimeType(const QByteArray &data)
{
    const auto startsWith = [&data](const char *magic, int length, int offset = 0) {
        return data.size() >= offset + length && data.mid(offset, length) == QByteArray(magic, length);
    };
    if (startsWith("\xff\xd8\xff", 3))
        return data.size() > 3 && static_cast<unsigned char>(data.at(3)) == 0xf7
                   ? QString() : QStringLiteral("image/jpeg");
    if (startsWith("\x89PNG\r\n\x1a\n", 8))
        return QStringLiteral("image/png");
    if (startsWith("GIF", 3))
        return QStringLiteral("image/gif");
    if (startsWith("RIFF", 4) && startsWith("WEBP", 4, 8))
        return QStringLiteral("image/webp");
    if (startsWith("BM", 2) && data.size() >= 26)
        return QStringLiteral("image/bmp");
    return {};
}

/** 去除 UTF-8 BOM，与 Pi 读取文本附件的行为保持一致。 */
QString decodeTextFile(QByteArray data)
{
    if (data.startsWith("\xef\xbb\xbf"))
        data.remove(0, 3);
    return QString::fromUtf8(data);
}
} // namespace

Q_LOGGING_CATEGORY(agentControllerLog, "pidesktop.agent")

/**
 * 建立进程、RPC 与聊天模型之间的单向事件连接。
 */
AgentSessionController::AgentSessionController(PiProcess *process, PiRpcClient *rpcClient,
                                               ChatModel *chatModel, QObject *parent)
    : QObject(parent)
    , m_process(process)
    , m_rpcClient(rpcClient)
    , m_chatModel(chatModel)
    , m_statusText(tr("正在连接 Pi…"))
{
    Q_ASSERT(m_process && m_rpcClient && m_chatModel);

    m_workingTimer.setParent(this);
    m_workingTimer.setObjectName(QStringLiteral("workingWordTimer"));
    m_workingTimer.setInterval(10000);
    connect(&m_workingTimer, &QTimer::timeout, this, &AgentSessionController::rotateWorkingText);
    qCInfo(agentControllerLog) << "[WorkingIndicator] initialized; intervalMs=10000; words=20; repeatPrevious=false";

    m_diagnosticTimer.setInterval(1000);
    m_diagnosticTimer.setSingleShot(true);
    connect(&m_diagnosticTimer, &QTimer::timeout, this, &AgentSessionController::flushDiagnostics);
    connect(m_process, &PiProcess::errorReceived, this, [this](const QByteArray &chunk) {
        // 高频 stderr 只累积尾部，定时更新同一张诊断卡片，不逐块写日志。
        m_diagnostics = (m_diagnostics + chunk).right(16 * 1024);
        if (!m_diagnosticTimer.isActive())
            m_diagnosticTimer.start();
    });
    connect(m_rpcClient, &PiRpcClient::eventReceived, this, &AgentSessionController::handleEvent);
    connect(m_rpcClient, &PiRpcClient::protocolError, this, [this](const QString &message) {
        reportRuntimeError(message);
    });
    connect(m_process, &PiProcess::started, this, [this] {
        qCInfo(agentControllerLog) << "[AgentSession] Pi 已连接，正在请求状态和消息";
        setStatusText(tr("Pi 已连接"));
        emit connectedChanged();
        m_diagnostics.clear();
        m_diagnosticTimer.stop();
        m_ready = false;
        m_initialStateRequest = m_rpcClient->requestState();
        m_initialMessagesRequest = m_rpcClient->requestMessages();
        m_statsSupported = true;
        m_sessionStats.clear();
        emit sessionStatsChanged();
    });
    connect(m_process, &PiProcess::runningChanged, this, [this] {
        if (!m_process->running())
            m_ready = false;
        emit connectedChanged();
    });
    connect(m_process, &PiProcess::processError, this, [this](const QString &message) {
        m_ready = false;
        emit connectedChanged();
        setBusy(false);
        setStatusText(tr("Pi 连接失败"));
        reportRuntimeError(message);
        m_chatModel->finishAssistant({}, false);
        flushDiagnostics();
    });
    connect(m_process, &PiProcess::processExited, this, [this](int exitCode) {
        setBusy(false);
        setStatusText(tr("Pi 已退出（%1）").arg(exitCode));
        m_statsRequestId.clear();
        flushDiagnostics();
        const bool unexpected = !m_process->stopping() && exitCode != 0;
        m_chatModel->finishAssistant({}, unexpected);
        if (unexpected)
            reportRuntimeError(tr("Pi 进程异常退出，退出码：%1").arg(exitCode));
    });
}

/**
 * 返回是否处于 Agent 运行期。
 */
bool AgentSessionController::busy() const
{
    return m_busy;
}

/**
 * 使用进程状态表示 RPC 连接可用性。
 */
bool AgentSessionController::connected() const
{
    return m_process->running() && !m_process->stopping() && m_ready;
}

/**
 * 返回当前状态栏文本。
 */
QString AgentSessionController::statusText() const
{
    return m_statusText;
}

/**
 * 未命名 Session 返回适合 UI 的默认名称。
 */
QString AgentSessionController::sessionName() const
{
    return m_sessionName.isEmpty() ? tr("新会话") : m_sessionName;
}

/**
 * 返回 Pi 报告的 Session 文件路径。
 */
QString AgentSessionController::sessionFile() const
{
    return m_sessionFile;
}

/**
 * 未获得模型状态时显示占位名称。
 */
QString AgentSessionController::modelName() const
{
    return m_modelName.isEmpty() ? tr("未选择模型") : m_modelName;
}

/**
 * 返回 Pi 当前思考等级，未获取时返回空字符串。
 */
QString AgentSessionController::thinkingLevel() const
{
    return m_thinkingLevel;
}

/**
 * 校验输入和连接，发送成功后才把用户消息加入 UI。
 */
bool AgentSessionController::prompt(const QString &text, bool followUp)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        qCInfo(agentControllerLog) << "[AgentSession] 跳过 Prompt：输入为空";
        return false;
    }
    if (!connected()) {
        const QString message = tr("Pi 尚未连接，无法发送消息。");
        qCWarning(agentControllerLog) << "[AgentSession]" << message;
        m_chatModel->appendSystemMessage(message, true);
        return false;
    }
    // 桌面端内置斜杠命令不发送给 Pi，而是映射到对应 RPC 调用。
    if (handleBuiltinCommand(trimmed)) {
        qCInfo(agentControllerLog) << "[AgentBuiltin] handled; text=" << trimmed;
        return true;
    }
    // 由 Pi 负责队列调度及命令展开，UI 不提前把排队文本伪装成已执行消息。
    const bool wasBusy = m_busy;
    const QString behavior = followUp ? QStringLiteral("followUp") : QStringLiteral("steer");
    QString message;
    QJsonArray images;
    buildPromptPayload(trimmed, message, images);
    QJsonObject command{
        {QStringLiteral("type"), QStringLiteral("prompt")},
        {QStringLiteral("message"), message},
        {QStringLiteral("streamingBehavior"), behavior}};
    if (!images.isEmpty())
        command.insert(QStringLiteral("images"), images);
    const QString id = m_rpcClient->sendCommand(command);
    if (id.isEmpty())
        return false;
    m_promptRequests.insert(id, {trimmed, wasBusy});
    if (!m_attachments.isEmpty())
        clearAttachments();
    qCInfo(agentControllerLog) << "[PromptQueue] 已提交；busy=" << wasBusy << "behavior=" << behavior;
    if (!wasBusy) {
        setBusy(true);
        m_runHadError = false;
        m_promptElapsed.start();
        m_firstEventMs = -1;
        m_firstTextMs = -1;
        setStatusText(m_workingText + QStringLiteral("…"));
        qCInfo(agentControllerLog) << "[AgentSession] Prompt 已提交，字符数:" << trimmed.size();
    }
    return true;
}

/** 返回服务端队列摘要，不在本地推测投递时机。 */
QString AgentSessionController::queueText() const
{
    return m_queueText;
}

/** 清空服务端队列并通过响应恢复草稿。 */
void AgentSessionController::retrieveQueue()
{
    if (!connected()) {
        qCWarning(agentControllerLog) << "[PromptQueue] 取回失败：未连接";
        return;
    }
    m_rpcClient->sendCommand({{QStringLiteral("type"), QStringLiteral("clear_queue")}});
    qCInfo(agentControllerLog) << "[PromptQueue] 已请求取回队列";
}

/**
 * Agent 空闲时不发送多余 abort 命令。
 */
void AgentSessionController::abort()
{
    if (!m_busy) {
        qCInfo(agentControllerLog) << "[AgentSession] 跳过中止：Agent 当前空闲";
        return;
    }
    // Pi 的 abort 不清除队列；必须先清队列，否则中止后可能自动续跑。
    retrieveQueue();
    m_rpcClient->abort();
    setStatusText(tr("正在中止…"));
    qCInfo(agentControllerLog) << "[AgentSession] 已发送中止请求";
}

/**
 * 仅在空闲连接状态下请求创建新 Session。
 */
void AgentSessionController::newSession()
{
    if (!connected() || m_busy) {
        qCWarning(agentControllerLog) << "[AgentSession] 无法新建 Session，连接:" << connected()
                                      << "忙碌:" << m_busy;
        return;
    }
    m_rpcClient->newSession();
    setStatusText(tr("正在新建会话…"));
}

/**
 * 校验历史文件路径后调用 Pi 的 switch_session，Pi 仍是唯一事实源。
 */
void AgentSessionController::switchSession(const QString &path)
{
    if (!connected() || m_busy || path.trimmed().isEmpty() || path == m_sessionFile) {
        qCInfo(agentControllerLog) << "[AgentSession] 跳过 Session 切换，连接:" << connected()
                                   << "忙碌:" << m_busy << "路径有效:" << !path.trimmed().isEmpty();
        return;
    }
    m_rpcClient->switchSession(path);
    setStatusText(tr("正在切换会话…"));
}

/** 重启窗口期间禁止向旧目录发消息，并清除旧会话的展示状态。 */
void AgentSessionController::prepareWorkspaceSwitch()
{
    m_ready = false;
    m_initialStateRequest.clear();
    m_initialMessagesRequest.clear();
    m_sessionFile.clear();
    m_sessionName.clear();
    m_queueText.clear();
    m_sessionStats.clear();
    m_statsRequestId.clear();
    // 切目录时 Pi 命令会变化，但内置命令必须保留。
    m_commands = builtinCommands();
    m_attachments.clear();
    m_thinkingLevel.clear();
    m_currentProvider.clear();
    m_currentModelId.clear();
    m_pendingThinkingLevel.clear();
    m_pendingSessionName.clear();
    m_chatModel->clear();
    emit sessionChanged();
    emit queueChanged();
    emit sessionStatsChanged();
    emit commandsChanged();
    emit attachmentsChanged();
    emit thinkingLevelChanged();
    emit connectedChanged();
    setStatusText(tr("正在切换工作目录…"));
    qCInfo(agentControllerLog) << "[ProjectWorkspace] 已关闭旧会话提交入口";
}

/**
 * 将 QML QVariantMap 转为 JSON 字段并保持扩展请求 ID 不变。
 */
void AgentSessionController::respondToExtension(const QString &id, const QVariantMap &result)
{
    m_rpcClient->respondToExtension(id, QJsonObject::fromVariantMap(result));
}

/**
 * 根据 Pi RPC type 更新模型、工具卡片和状态栏。
 */
void AgentSessionController::handleEvent(const PiEvent &event)
{
    const QString type = event.type();
    const QJsonObject &payload = event.payload();
    if (m_promptElapsed.isValid() && m_firstEventMs < 0 && type != QStringLiteral("response"))
        m_firstEventMs = m_promptElapsed.elapsed();

    if (type == QStringLiteral("response")) {
        handleResponse(payload);
    } else if (type == QStringLiteral("queue_update")) {
        QStringList lines;
        for (const QJsonValue &value : payload.value(QStringLiteral("steering")).toArray())
            lines.append(tr("引导：%1").arg(value.toString()));
        for (const QJsonValue &value : payload.value(QStringLiteral("followUp")).toArray())
            lines.append(tr("后续：%1").arg(value.toString()));
        m_queueText = lines.join(u'\n');
        emit queueChanged();
    } else if (type == QStringLiteral("agent_start")) {
        setBusy(true);
        setStatusText(m_workingText + QStringLiteral("…"));
    } else if (type == QStringLiteral("agent_settled")) {
        setBusy(false);
        m_chatModel->finishAssistant({}, false);
        setStatusText(m_runHadError ? tr("本轮已结束，错误详情见会话") : tr("就绪"));
        m_rpcClient->requestState();
        refreshSessionStats();
        flushDiagnostics();
        emit sessionsChanged();
        // 只在整轮结束时输出耗时汇总，首事件和首正文耗时包含 Agent 与模型等待。
        qCInfo(agentControllerLog) << "[AgentTiming] settled; hadError=" << m_runHadError
                                  << "firstEventMs=" << m_firstEventMs << "firstTextMs=" << m_firstTextMs
                                  << "totalMs=" << (m_promptElapsed.isValid() ? m_promptElapsed.elapsed() : -1);
        m_promptElapsed.invalidate();
    } else if (type == QStringLiteral("message_start")) {
        const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("role")).toString() == QStringLiteral("user")) {
            m_chatModel->appendUserMessage(extractText(message.value(QStringLiteral("content"))));
        } else if (message.value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) {
            m_chatModel->ensureStreamingAssistant();
            setStatusText(m_workingText + QStringLiteral("…"));
        }
    } else if (type == QStringLiteral("message_update")) {
        const QJsonObject update = payload.value(QStringLiteral("assistantMessageEvent")).toObject();
        if (update.value(QStringLiteral("type")).toString() == QStringLiteral("text_delta")) {
            if (m_promptElapsed.isValid() && m_firstTextMs < 0)
                m_firstTextMs = m_promptElapsed.elapsed();
            m_chatModel->appendAssistantDelta(update.value(QStringLiteral("delta")).toString());
        }
    } else if (type == QStringLiteral("message_end")) {
        const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("role")).toString() == QStringLiteral("assistant")) {
            const QString stopReason = message.value(QStringLiteral("stopReason")).toString();
            const bool failed = stopReason == QStringLiteral("error");
            QString text = extractText(message.value(QStringLiteral("content")));
            if (failed) {
                const QString detail = message.value(QStringLiteral("errorMessage")).toString();
                text += (text.isEmpty() ? QString() : QStringLiteral("\n\n"))
                        + (detail.isEmpty() ? tr("Pi 请求失败，未返回错误详情。") : detail);
                m_runHadError = true;
                setStatusText(tr("模型请求失败，详情见会话"));
                qCWarning(agentControllerLog) << "[AgentSession] assistant failed:" << detail;
            }
            m_chatModel->finishAssistant(text, failed);
        }
    } else if (type == QStringLiteral("tool_execution_start")) {
        const QString input = QString::fromUtf8(
            QJsonDocument(payload.value(QStringLiteral("args")).toObject()).toJson(QJsonDocument::Indented));
        m_chatModel->startTool(payload.value(QStringLiteral("toolCallId")).toString(),
                               payload.value(QStringLiteral("toolName")).toString(), input);
        setStatusText(tr("正在执行工具：%1").arg(payload.value(QStringLiteral("toolName")).toString()));
    } else if (type == QStringLiteral("tool_execution_update")) {
        const QJsonObject result = payload.value(QStringLiteral("partialResult")).toObject();
        m_chatModel->updateTool(payload.value(QStringLiteral("toolCallId")).toString(),
                                extractText(result.value(QStringLiteral("content"))));
    } else if (type == QStringLiteral("tool_execution_end")) {
        const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
        m_chatModel->finishTool(payload.value(QStringLiteral("toolCallId")).toString(),
                                extractText(result.value(QStringLiteral("content"))),
                                payload.value(QStringLiteral("isError")).toBool());
        if (payload.value(QStringLiteral("isError")).toBool()) {
            m_runHadError = true;
            qCWarning(agentControllerLog) << "[AgentTool] failed; id=" << payload.value(QStringLiteral("toolCallId"))
                                          << "name=" << payload.value(QStringLiteral("toolName"));
        }
    } else if (type == QStringLiteral("turn_end")) {
        refreshSessionStats();
    } else if (type == QStringLiteral("auto_retry_start") || type == QStringLiteral("summarization_retry_scheduled")) {
        const QString status = tr("%1重试 %2/%3，等待 %4 秒：%5")
            .arg(type == QStringLiteral("auto_retry_start") ? tr("模型") : tr("摘要"))
            .arg(payload.value(QStringLiteral("attempt")).toInt())
            .arg(payload.value(QStringLiteral("maxAttempts")).toInt())
            .arg(payload.value(QStringLiteral("delayMs")).toDouble() / 1000.0, 0, 'f', 1)
            .arg(payload.value(QStringLiteral("errorMessage")).toString());
        setStatusText(status);
        m_chatModel->appendSystemMessage(status, true);
        qCWarning(agentControllerLog) << "[AgentRetry]" << status;
    } else if (type == QStringLiteral("auto_retry_end")) {
        if (!payload.value(QStringLiteral("success")).toBool())
            reportRuntimeError(tr("自动重试失败：%1").arg(payload.value(QStringLiteral("finalError")).toString()));
        else
            setStatusText(tr("重试成功，正在完成本轮"));
    } else if (type == QStringLiteral("compaction_start")) {
        setStatusText(tr("正在压缩上下文…"));
        m_chatModel->appendSystemMessage(m_statusText);
        qCInfo(agentControllerLog) << "[AgentCompaction] started; reason=" << payload.value(QStringLiteral("reason"));
    } else if (type == QStringLiteral("compaction_end")) {
        if (!payload.value(QStringLiteral("errorMessage")).toString().isEmpty())
            reportRuntimeError(tr("上下文压缩失败：%1").arg(payload.value(QStringLiteral("errorMessage")).toString()));
        else {
            setStatusText(payload.value(QStringLiteral("aborted")).toBool() ? tr("上下文压缩已取消") : tr("上下文压缩完成"));
            m_chatModel->appendSystemMessage(m_statusText);
            qCInfo(agentControllerLog) << "[AgentCompaction]" << m_statusText;
        }
        refreshSessionStats();
    } else if (type == QStringLiteral("extension_ui_request")) {
        handleExtensionUi(payload);
    } else if (type == QStringLiteral("extension_error")) {
        const QString message = tr("扩展执行失败：%1").arg(payload.value(QStringLiteral("error")).toString());
        reportRuntimeError(message);
    }
}

/**
 * 处理状态、历史消息和 Session 切换响应，并统一展示命令失败原因。
 */
void AgentSessionController::handleResponse(const QJsonObject &payload)
{
    const QString command = payload.value(QStringLiteral("command")).toString();
    if (command == QStringLiteral("get_session_stats")) {
        // 只接收最近一次统计请求，避免切换会话后旧统计覆盖新面板。
        if (m_statsRequestId.isEmpty() || payload.value(QStringLiteral("id")).toString() != m_statsRequestId)
            return;
        m_statsRequestId.clear();
        if (!payload.value(QStringLiteral("success")).toBool()) {
            m_statsSupported = false;
            m_sessionStats.clear();
            emit sessionStatsChanged();
            qCWarning(agentControllerLog) << "[AgentStats] unavailable; automatic refresh disabled until reconnect:"
                                          << payload.value(QStringLiteral("error"));
            return; // 统计失败不能误清除 Agent 的忙碌状态。
        }
        m_sessionStats = payload.value(QStringLiteral("data")).toObject().toVariantMap();
        emit sessionStatsChanged();
        qCInfo(agentControllerLog) << "[AgentStats] updated; contextAvailable="
                                  << m_sessionStats.contains(QStringLiteral("contextUsage"));
        return;
    }
    if (command == QStringLiteral("get_commands")) {
        // 内置命令在前，Pi 命令去重后追加，保证两套命令都能补全。
        m_commands = builtinCommands();
        QSet<QString> names;
        for (const QVariant &entry : m_commands)
            names.insert(entry.toMap().value(QStringLiteral("name")).toString());
        if (payload.value(QStringLiteral("success")).toBool()) {
            const QJsonArray list = payload.value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("commands")).toArray();
            for (const QJsonValue &value : list) {
                const QJsonObject entry = value.toObject();
                const QString name = entry.value(QStringLiteral("name")).toString();
                if (name.isEmpty() || names.contains(name))
                    continue;
                names.insert(name);
                m_commands.append(QVariantMap{
                    {QStringLiteral("name"), name},
                    {QStringLiteral("invocation"), QStringLiteral("/") + name},
                    {QStringLiteral("description"), entry.value(QStringLiteral("description")).toString()},
                    {QStringLiteral("source"), entry.value(QStringLiteral("source")).toString()}});
            }
        } else {
            qCWarning(agentControllerLog) << "[AgentCommands] unavailable:"
                                         << payload.value(QStringLiteral("error")).toString();
        }
        emit commandsChanged();
        qCInfo(agentControllerLog) << "[AgentCommands] loaded; count=" << m_commands.size();
        return;
    }
    const QString requestId = payload.value(QStringLiteral("id")).toString();
    const bool knownPrompt = m_promptRequests.contains(requestId);
    const auto submitted = m_promptRequests.take(requestId);
    if (!payload.value(QStringLiteral("success")).toBool()) {
        if (knownPrompt)
            emit restoreDraftRequested(submitted.first);
        const QString message = tr("Pi 命令 %1 失败：%2")
                                    .arg(command, payload.value(QStringLiteral("error")).toString());
        qCWarning(agentControllerLog) << "[AgentSession] 运行时异常:" << message;
        if (command == QStringLiteral("prompt") && knownPrompt && !submitted.second) {
            setBusy(false);
            m_chatModel->finishAssistant({}, false);
        }
        reportRuntimeError(message);
        return;
    }

    // 只有新进程的初始化响应全部到齐，才允许在新目录提交消息。
    const bool initializationResponse = requestId == m_initialStateRequest
                                        || requestId == m_initialMessagesRequest;
    if (requestId == m_initialStateRequest)
        m_initialStateRequest.clear();
    if (requestId == m_initialMessagesRequest)
        m_initialMessagesRequest.clear();

    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    if (command == QStringLiteral("clear_queue")) {
        QStringList drafts;
        for (const QJsonValue &value : data.value(QStringLiteral("steering")).toArray())
            drafts.append(value.toString());
        for (const QJsonValue &value : data.value(QStringLiteral("followUp")).toArray())
            drafts.append(value.toString());
        if (!drafts.isEmpty())
            emit restoreDraftRequested(drafts.join(QStringLiteral("\n\n")));
        qCInfo(agentControllerLog) << "[PromptQueue] 取回成功；消息数:" << drafts.size();
    } else if (command == QStringLiteral("get_state")) {
        m_sessionFile = data.value(QStringLiteral("sessionFile")).toString();
        m_sessionName = data.value(QStringLiteral("sessionName")).toString();
        const QJsonObject model = data.value(QStringLiteral("model")).toObject();
        const QString provider = model.value(QStringLiteral("provider")).toString();
        const QString modelId = model.value(QStringLiteral("id")).toString();
        m_currentProvider = provider;
        m_currentModelId = modelId;
        m_modelName = provider.isEmpty() ? modelId : provider + u'/' + modelId;
        const QString level = data.value(QStringLiteral("thinkingLevel")).toString();
        if (!level.isEmpty() && level != m_thinkingLevel) {
            m_thinkingLevel = level;
            emit thinkingLevelChanged();
        }
        emit sessionChanged();
        emit modelNameChanged();
    } else if (command == QStringLiteral("get_available_models")) {
        QVariantList models;
        for (const QJsonValue &value : data.value(QStringLiteral("models")).toArray()) {
            const QJsonObject model = value.toObject();
            const QString provider = model.value(QStringLiteral("provider")).toString();
            const QString id = model.value(QStringLiteral("id")).toString();
            const QString name = model.value(QStringLiteral("name")).toString();
            models.append(QVariantMap{
                {QStringLiteral("provider"), provider},
                {QStringLiteral("id"), id},
                {QStringLiteral("value"), provider + u'/' + id},
                {QStringLiteral("label"), provider + u'/' + id
                     + (name.isEmpty() ? QString() : QStringLiteral("  ·  ") + name)},
                {QStringLiteral("current"), provider == m_currentProvider && id == m_currentModelId}});
        }
        emit modelSelectionRequested(models, m_currentProvider.isEmpty()
                                                  ? QString()
                                                  : m_currentProvider + u'/' + m_currentModelId);
        qCInfo(agentControllerLog) << "[AgentBuiltin] available models; count=" << models.size();
    } else if (command == QStringLiteral("set_model")) {
        m_currentProvider = data.value(QStringLiteral("provider")).toString();
        m_currentModelId = data.value(QStringLiteral("id")).toString();
        m_modelName = m_currentProvider.isEmpty() ? m_currentModelId
                                                  : m_currentProvider + u'/' + m_currentModelId;
        emit modelNameChanged();
        m_chatModel->appendSystemMessage(tr("已切换模型：%1").arg(m_modelName));
        qCInfo(agentControllerLog) << "[AgentBuiltin] model switched;" << m_modelName;
    } else if (command == QStringLiteral("get_available_thinking_levels")) {
        QVariantList levels;
        for (const QJsonValue &value : data.value(QStringLiteral("levels")).toArray())
            levels.append(value.toString());
        emit thinkingSelectionRequested(levels, m_thinkingLevel);
        qCInfo(agentControllerLog) << "[AgentBuiltin] available thinking levels; count=" << levels.size();
    } else if (command == QStringLiteral("set_thinking_level")) {
        m_thinkingLevel = m_pendingThinkingLevel;
        emit thinkingLevelChanged();
        m_chatModel->appendSystemMessage(tr("思考等级已设为：%1").arg(m_thinkingLevel));
        qCInfo(agentControllerLog) << "[AgentBuiltin] thinking level;" << m_thinkingLevel;
    } else if (command == QStringLiteral("compact")) {
        m_chatModel->appendSystemMessage(tr("上下文压缩完成。"));
        refreshSessionStats();
        qCInfo(agentControllerLog) << "[AgentBuiltin] compaction finished";
    } else if (command == QStringLiteral("export_html")) {
        const QString path = data.value(QStringLiteral("path")).toString();
        m_chatModel->appendSystemMessage(tr("已导出会话 HTML：%1").arg(path));
        qCInfo(agentControllerLog) << "[AgentBuiltin] exported;" << path;
    } else if (command == QStringLiteral("set_session_name")) {
        m_sessionName = m_pendingSessionName;
        emit sessionChanged();
        qCInfo(agentControllerLog) << "[AgentBuiltin] session renamed;" << m_sessionName;
    } else if (command == QStringLiteral("get_messages")) {
        m_chatModel->replaceFromMessages(data.value(QStringLiteral("messages")).toArray());
        qCInfo(agentControllerLog) << "[AgentSession] 历史消息加载成功，数量:"
                                   << data.value(QStringLiteral("messages")).toArray().size();
    } else if (command == QStringLiteral("switch_session") || command == QStringLiteral("new_session")) {
        if (data.value(QStringLiteral("cancelled")).toBool()) {
            setStatusText(tr("会话操作已被扩展取消"));
            qCInfo(agentControllerLog) << "[AgentSession] Session 操作被扩展取消:" << command;
            return;
        }
        m_statsRequestId.clear();
        m_sessionStats.clear();
        m_diagnostics.clear();
        m_diagnosticTimer.stop();
        emit sessionStatsChanged();
        m_chatModel->clear();
        m_rpcClient->requestState();
        m_rpcClient->requestMessages();
        refreshCommands();
        emit sessionsChanged();
        setStatusText(tr("会话已切换"));
        refreshSessionStats();
        if (command == QStringLiteral("new_session"))
            emit newSessionCreated();
        qCInfo(agentControllerLog) << "[AgentSession] Session 操作成功:" << command;
    }
    if (initializationResponse && !requestId.isEmpty()
        && m_initialStateRequest.isEmpty() && m_initialMessagesRequest.isEmpty()) {
        m_ready = true;
        emit connectedChanged();
        setStatusText(tr("就绪"));
        refreshSessionStats();
        refreshCommands();
        qCInfo(agentControllerLog) << "[ProjectWorkspace] 新目录会话初始化成功";
    }
}

/**
 * 对通知和编辑器设置立即处理，对阻塞对话转发给 QML。
 */
void AgentSessionController::handleExtensionUi(const QJsonObject &payload)
{
    const QString method = payload.value(QStringLiteral("method")).toString();
    if (method == QStringLiteral("notify")) {
        m_chatModel->appendSystemMessage(payload.value(QStringLiteral("message")).toString(),
                                         payload.value(QStringLiteral("notifyType")).toString() == QStringLiteral("error"));
    } else if (method == QStringLiteral("setStatus")) {
        const QString status = payload.value(QStringLiteral("statusText")).toString();
        if (!status.isEmpty()) {
            setStatusText(status);
        }
    } else if (method == QStringLiteral("set_editor_text")) {
        emit editorTextRequested(payload.value(QStringLiteral("text")).toString());
    } else if (method == QStringLiteral("select") || method == QStringLiteral("confirm")
               || method == QStringLiteral("input") || method == QStringLiteral("editor")) {
        emit extensionDialogRequested(payload.toVariantMap());
    }
}

/**
 * 避免重复状态信号触发无意义的 QML 绑定刷新。
 */
void AgentSessionController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    if (busy) {
        rotateWorkingText();
        m_workingTimer.start();
    } else {
        m_workingTimer.stop();
    }
    // 仅记录生命周期，不在每次换词或动画帧中写日志。
    qCInfo(agentControllerLog) << "[WorkingIndicator] rotation active=" << busy;
    emit busyChanged();
}

/** 每十秒无重复抽选提示词，保留工具执行、重试、压缩和错误等真实状态。 */
void AgentSessionController::rotateWorkingText()
{
    if (!m_busy)
        return;
    static const QStringList words = {
        QStringLiteral("Thinking"), QStringLiteral("Considering"), QStringLiteral("Contemplating"),
        QStringLiteral("Pondering"), QStringLiteral("Mulling"), QStringLiteral("Musing"),
        QStringLiteral("Ruminating"), QStringLiteral("Cogitating"), QStringLiteral("Cerebrating"),
        QStringLiteral("Deliberating"), QStringLiteral("Determining"), QStringLiteral("Inferring"),
        QStringLiteral("Envisioning"), QStringLiteral("Ideating"), QStringLiteral("Imagining"),
        QStringLiteral("Improvising"), QStringLiteral("Noodling"), QStringLiteral("Perusing"),
        QStringLiteral("Philosophising"), QStringLiteral("Pontificating")
    };
    const QString previous = m_workingText;
    const int previousIndex = words.indexOf(previous);
    // 在排除上次结果的集合中等概率抽样，无需随机重试循环。
    int nextIndex = QRandomGenerator::global()->bounded(static_cast<int>(words.size()) - (previousIndex >= 0 ? 1 : 0));
    if (previousIndex >= 0 && nextIndex >= previousIndex)
        ++nextIndex;
    m_workingText = words.at(nextIndex);
    emit workingTextChanged();
    if (m_statusText == previous + QStringLiteral("…"))
        setStatusText(m_workingText + QStringLiteral("…"));
}

/**
 * 避免重复状态栏更新。
 */
void AgentSessionController::setStatusText(const QString &status)
{
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    emit statusTextChanged();
}

/** 返回由十秒定时器更新的英文工作提示，不随流式增量频繁变化。 */
QString AgentSessionController::workingText() const
{
    return m_workingText;
}

/** 返回当前上下文估计与会话累计统计，缺失值保留未知状态。 */
QVariantMap AgentSessionController::sessionStats() const
{
    return m_sessionStats;
}

/** 返回 Pi 上报的可用命令，供编辑器补全。 */
QVariantList AgentSessionController::commands() const
{
    return m_commands;
}

/** 返回待随下一条 Prompt 发送的附件。 */
QVariantList AgentSessionController::attachments() const
{
    return m_attachments;
}

/** 在会话边界请求真实统计，不按流式增量轮询。 */
void AgentSessionController::refreshSessionStats()
{
    if (!connected() || !m_statsSupported)
        return;
    m_statsRequestId = m_rpcClient->requestSessionStats();
}

/** 请求一次命令列表；先填充内置命令，保证 Pi 未连接时补全仍可用。 */
void AgentSessionController::refreshCommands()
{
    // 内置命令必须始终可用，即使 Pi 还没返回或没有扩展命令。
    m_commands = builtinCommands();
    emit commandsChanged();
    if (!connected())
        return;
    m_rpcClient->requestCommands();
}

/**
 * 添加本地文件或图片附件；文本在发送时展开为 <file> 块，图片转为 base64。
 */
bool AgentSessionController::attachFiles(const QVariantList &fileUrls)
{
    if (!connected()) {
        m_chatModel->appendSystemMessage(tr("Pi 尚未连接，无法添加附件。"), true);
        return false;
    }
    bool added = false;
    for (const QVariant &value : fileUrls) {
        QString path = value.toString();
        const QUrl url(path);
        if (url.isLocalFile())
            path = url.toLocalFile();
        path = QDir::fromNativeSeparators(path);
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            m_chatModel->appendSystemMessage(tr("附件不存在或不是文件：%1").arg(path), true);
            continue;
        }
        if (info.size() > kMaximumAttachmentBytes) {
            m_chatModel->appendSystemMessage(
                tr("附件超过 10 MiB，已跳过：%1").arg(info.fileName()), true);
            continue;
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            m_chatModel->appendSystemMessage(tr("无法读取附件：%1").arg(info.fileName()), true);
            continue;
        }
        const QByteArray data = file.readAll();
        if (data.isEmpty())
            continue;
        const QString mimeType = detectImageMimeType(data);
        QVariantMap entry{
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("path"), QDir::toNativeSeparators(info.absoluteFilePath())},
            {QStringLiteral("image"), !mimeType.isEmpty()},
            {QStringLiteral("size"), info.size()}};
        if (!mimeType.isEmpty()) {
            entry.insert(QStringLiteral("mimeType"), mimeType);
            entry.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
        } else {
            entry.insert(QStringLiteral("text"), decodeTextFile(data));
        }
        m_attachments.append(entry);
        added = true;
    }
    if (added) {
        emit attachmentsChanged();
        qCInfo(agentControllerLog) << "[AgentAttachments] added; count=" << m_attachments.size();
    }
    return added;
}

/** 移除指定附件，越界时忽略。 */
void AgentSessionController::removeAttachment(int index)
{
    if (index < 0 || index >= m_attachments.size())
        return;
    m_attachments.removeAt(index);
    emit attachmentsChanged();
}

/** 清空待发送附件。 */
void AgentSessionController::clearAttachments()
{
    if (m_attachments.isEmpty())
        return;
    m_attachments.clear();
    emit attachmentsChanged();
}

/**
 * 切换到用户选定的模型，结果由 set_model 响应回写到状态栏与聊天。
 */
void AgentSessionController::selectModel(const QString &provider, const QString &modelId)
{
    if (!connected() || m_busy || provider.trimmed().isEmpty() || modelId.trimmed().isEmpty())
        return;
    m_rpcClient->setModel(provider, modelId);
    setStatusText(tr("正在切换模型…"));
    qCInfo(agentControllerLog) << "[AgentBuiltin] select model; provider=" << provider
                              << "id=" << modelId;
}

/**
 * 记录待确认的思考等级并发送 set_thinking_level；响应不带数据，回写时依赖该暂存值。
 */
void AgentSessionController::selectThinkingLevel(const QString &level)
{
    if (!connected() || m_busy || level.trimmed().isEmpty())
        return;
    m_pendingThinkingLevel = level.trimmed();
    m_rpcClient->setThinkingLevel(m_pendingThinkingLevel);
    setStatusText(tr("正在设置思考等级…"));
    qCInfo(agentControllerLog) << "[AgentBuiltin] select thinking level; level=" << m_pendingThinkingLevel;
}

/**
 * 返回桌面端内置命令，与 Pi 上报的命令合并用于编辑器补全。
 */
QVariantList AgentSessionController::builtinCommands()
{
    const auto entry = [](const QString &name, const QString &description) {
        return QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("invocation"), QStringLiteral("/") + name},
            {QStringLiteral("description"), description},
            {QStringLiteral("source"), QStringLiteral("desktop")}};
    };
    return {
        entry(QStringLiteral("new"), tr("新建会话")),
        entry(QStringLiteral("compact"), tr("压缩上下文")),
        entry(QStringLiteral("model"), tr("切换模型")),
        entry(QStringLiteral("thinking"), tr("设置思考等级")),
        entry(QStringLiteral("name"), tr("重命名当前会话")),
        entry(QStringLiteral("export"), tr("导出会话为 HTML")),
        entry(QStringLiteral("abort"), tr("中止当前任务")),
    };
}

/**
 * 解析并执行内置命令；不匹配任何内置命令时返回 false，交回 Pi 处理。
 */
bool AgentSessionController::handleBuiltinCommand(const QString &text)
{
    if (!text.startsWith(QLatin1Char('/')))
        return false;
    const int separator = text.indexOf(QLatin1Char(' '));
    const QString token = (separator < 0 ? text.mid(1) : text.mid(1, separator - 1)).trimmed().toLower();
    const QString argument = separator < 0 ? QString() : text.mid(separator + 1).trimmed();
    static const QStringList names{
        QStringLiteral("new"), QStringLiteral("compact"), QStringLiteral("abort"),
        QStringLiteral("name"), QStringLiteral("export"), QStringLiteral("model"),
        QStringLiteral("thinking")};
    if (!names.contains(token))
        return false;

    const auto requireIdle = [this, token]() {
        if (!m_busy)
            return true;
        m_chatModel->appendSystemMessage(tr("当前任务进行中，无法执行 /%1。").arg(token), true);
        return false;
    };

    if (token == QStringLiteral("abort")) {
        abort();
    } else if (token == QStringLiteral("new")) {
        if (requireIdle())
            newSession();
    } else if (token == QStringLiteral("compact")) {
        if (requireIdle()) {
            m_rpcClient->compact();
            setStatusText(tr("正在压缩上下文…"));
        }
    } else if (token == QStringLiteral("export")) {
        if (requireIdle())
            m_rpcClient->exportHtml();
    } else if (token == QStringLiteral("model")) {
        if (requireIdle())
            m_rpcClient->requestAvailableModels();
    } else if (token == QStringLiteral("thinking")) {
        if (requireIdle())
            m_rpcClient->requestThinkingLevels();
    } else if (token == QStringLiteral("name")) {
        if (argument.isEmpty()) {
            m_chatModel->appendSystemMessage(tr("用法：/name <会话名称>"), true);
        } else {
            m_pendingSessionName = argument;
            m_rpcClient->setSessionName(argument);
        }
    }
    return true;
}

/**
 * 按 Pi CLI 的约定展开附件：文本包成 <file>，图片附加 base64 并留下文件引用。
 */
void AgentSessionController::buildPromptPayload(const QString &text, QString &message,
                                                QJsonArray &images) const
{
    QString filePrefix;
    QString imageReferences;
    for (const QVariant &value : m_attachments) {
        const QVariantMap entry = value.toMap();
        const QString path = entry.value(QStringLiteral("path")).toString();
        if (entry.value(QStringLiteral("image")).toBool()) {
            images.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image")},
                {QStringLiteral("data"), entry.value(QStringLiteral("data")).toString()},
                {QStringLiteral("mimeType"), entry.value(QStringLiteral("mimeType")).toString()}});
            imageReferences += QStringLiteral("<file name=\"%1\"></file>\n").arg(path);
        } else {
            filePrefix += QStringLiteral("<file name=\"%1\">\n%2\n</file>\n")
                              .arg(path, entry.value(QStringLiteral("text")).toString());
        }
    }
    message = filePrefix + text + (imageReferences.isEmpty() ? QString()
                                                             : QStringLiteral("\n") + imageReferences);
}

/** 定时刷新单张标准错误卡片，保留最近十六 KiB，避免控制台诊断刷满消息列表。 */
void AgentSessionController::flushDiagnostics()
{
    m_diagnosticTimer.stop();
    if (m_diagnostics.trimmed().isEmpty())
        return;
    m_chatModel->updateProcessDiagnostic(tr("Pi 标准错误 / 诊断（最近 16 KiB，不一定代表失败）：\n%1")
                                            .arg(QString::fromUtf8(m_diagnostics)));
}

/** 将真实运行失败同时写入状态栏、聊天列表和排故日志。 */
void AgentSessionController::reportRuntimeError(const QString &message)
{
    m_runHadError = true;
    setStatusText(message);
    m_chatModel->appendSystemMessage(message, true);
    qCWarning(agentControllerLog) << "[AgentRuntime]" << message;
}

/**
 * 从字符串或 TextContent 数组提取 Markdown 文本。
 */
QString AgentSessionController::extractText(const QJsonValue &content)
{
    if (content.isString()) {
        return content.toString();
    }
    QStringList parts;
    for (const QJsonValue &value : content.toArray()) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            parts.append(block.value(QStringLiteral("text")).toString());
        }
    }
    return parts.join(QStringLiteral("\n\n"));
}
