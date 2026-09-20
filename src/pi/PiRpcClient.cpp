#include "PiRpcClient.h"

#include "PiProcess.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(piRpcLog, "pidesktop.rpc")

/**
 * 将 PiProcess 的字节流连接到 JSONL 解析器。
 */
PiRpcClient::PiRpcClient(PiProcess *process, QObject *parent)
    : QObject(parent)
    , m_process(process)
{
    Q_ASSERT(m_process);
    m_dispatchTimer.setSingleShot(true);
    m_dispatchTimer.setInterval(1);
    connect(&m_dispatchTimer, &QTimer::timeout, this, &PiRpcClient::drainOutput);
    connect(m_process, &PiProcess::started, this, [this] {
        m_dispatchTimer.stop();
        m_buffer.clear();
        m_parseErrorCount = 0;
    });
    connect(m_process, &PiProcess::outputReceived, this, &PiRpcClient::consumeOutput);
    qCInfo(piRpcLog) << "[PiRpcDispatch] initialized; budgetMs=4; maxRecordsPerSlice=64";
}

/**
 * 自动补充请求 ID，将紧凑 JSON 和 LF 作为一条协议记录写入进程。
 */
QString PiRpcClient::sendCommand(QJsonObject command)
{
    if (!command.contains(QStringLiteral("type"))) {
        const QString message = tr("RPC 命令缺少 type 字段。");
        qCWarning(piRpcLog) << "[PiRpc] 配置无效:" << message;
        emit protocolError(message);
        return {};
    }

    QString id = command.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        id = QStringLiteral("desktop-%1").arg(m_nextRequestId++);
        command.insert(QStringLiteral("id"), id);
    }

    QByteArray record = QJsonDocument(command).toJson(QJsonDocument::Compact);
    record.append('\n');
    if (!m_process->write(record)) {
        const QString message = tr("Pi RPC 未连接，命令“%1”发送失败。")
                                    .arg(command.value(QStringLiteral("type")).toString());
        qCWarning(piRpcLog) << "[PiRpc] 发送失败:" << message;
        emit protocolError(message);
        return {};
    }
    return id;
}

/**
 * 构造 prompt 命令。
 */
QString PiRpcClient::sendPrompt(const QString &prompt)
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("prompt")},
                        {QStringLiteral("message"), prompt}});
}

/**
 * 构造 abort 命令。
 */
QString PiRpcClient::abort()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("abort")}});
}

/**
 * 构造 new_session 命令。
 */
QString PiRpcClient::newSession()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("new_session")}});
}

/**
 * 构造 switch_session 命令。
 */
QString PiRpcClient::switchSession(const QString &sessionPath)
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("switch_session")},
                        {QStringLiteral("sessionPath"), sessionPath}});
}

/**
 * 构造 get_state 命令。
 */
QString PiRpcClient::requestState()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("get_state")}});
}

/**
 * 构造 get_messages 命令。
 */
QString PiRpcClient::requestMessages()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("get_messages")} });
}

/**
 * 构造 get_session_stats 命令。
 */
QString PiRpcClient::requestSessionStats()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("get_session_stats")} });
}

/**
 * 构造 get_commands 命令，返回扩展命令、提示模板与技能。
 */
QString PiRpcClient::requestCommands()
{
    return sendCommand({{QStringLiteral("type"), QStringLiteral("get_commands")}});
}

/**
 * 合并扩展 UI 对话 ID 与调用方提供的结果字段。
 */
QString PiRpcClient::respondToExtension(const QString &id, const QJsonObject &result)
{
    QJsonObject response = result;
    response.insert(QStringLiteral("type"), QStringLiteral("extension_ui_response"));
    response.insert(QStringLiteral("id"), id);
    // extension_ui_response 不是普通请求，不覆盖扩展提供的关联 ID。
    QByteArray record = QJsonDocument(response).toJson(QJsonDocument::Compact);
    record.append('\n');
    if (!m_process->write(record)) {
        const QString message = tr("扩展 UI 响应发送失败。");
        qCWarning(piRpcLog) << "[PiRpc]" << message;
        emit protocolError(message);
        return {};
    }
    return id;
}

/**
 * 严格仅以字节 0x0A 分帧，并保留尚未完成的尾部记录。
 */
void PiRpcClient::consumeOutput(const QByteArray &data)
{
    m_buffer.append(data);
    constexpr qsizetype maximumRecordSize = 64 * 1024 * 1024;
    if (m_buffer.size() > maximumRecordSize && !m_buffer.contains('\n')) {
        m_buffer.clear();
        const QString message = tr("Pi RPC 单条 JSONL 记录超过 64 MiB，已丢弃。");
        qCWarning(piRpcLog) << "[PiRpc]" << message;
        emit protocolError(message);
        return;
    }
    if (!m_dispatchTimer.isActive())
        m_dispatchTimer.start();
}

/**
 * 一轮最多处理六十四条或四毫秒；不截断单条事件，按原顺序延迟继续处理。
 */
void PiRpcClient::drainOutput()
{
    QElapsedTimer budget;
    budget.start();
    int records = 0;
    while (records < 64 && budget.elapsed() < 4) {
        const qsizetype newlineIndex = m_buffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.trimmed().isEmpty()) {
            parseLine(std::move(line));
        }
        ++records;
    }
    if (m_buffer.contains('\n'))
        m_dispatchTimer.start();
}

/**
 * 将 JSON 对象包装成 PiEvent；首个解析错误写日志，后续只累计以避免刷屏。
 */
void PiRpcClient::parseLine(QByteArray line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        ++m_parseErrorCount;
        const QString message = tr("Pi RPC JSON 解析失败：%1").arg(parseError.errorString());
        if (m_parseErrorCount == 1) {
            qCWarning(piRpcLog) << "[PiRpc]" << message << "记录摘要:" << line.left(300);
            emit protocolError(message);
        }
        return;
    }

    emit eventReceived(PiEvent(document.object()));
}
