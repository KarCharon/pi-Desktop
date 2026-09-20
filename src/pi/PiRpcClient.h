#pragma once

#include "PiEvent.h"

#include <QObject>
#include <QJsonObject>
#include <QTimer>

class PiProcess;

/**
 * 实现 Pi RPC 的 JSON 编解码、严格 LF JSONL 分帧和请求编号。
 */
class PiRpcClient final : public QObject
{
    Q_OBJECT

public:
    /**
     * 将 RPC 客户端绑定到一个 PiProcess。
     */
    explicit PiRpcClient(PiProcess *process, QObject *parent = nullptr);

    /**
     * 发送任意 RPC 命令并返回生成的请求编号；失败时返回空字符串。
     */
    QString sendCommand(QJsonObject command);

    /**
     * 向当前会话发送 Prompt。
     */
    QString sendPrompt(const QString &prompt);

    /**
     * 中止当前 Agent 操作。
     */
    QString abort();

    /**
     * 创建新的 Pi Session。
     */
    QString newSession();

    /**
     * 切换到指定 Pi Session 文件。
     */
    QString switchSession(const QString &sessionPath);

    /**
     * 请求当前会话状态。
     */
    QString requestState();

    /**
     * 请求当前分支中的全部消息。
     */
    QString requestMessages();

    /**
     * 请求当前 Session 的 token 和上下文统计。
     */
    QString requestSessionStats();

    /**
     * 请求可用命令、提示模板与技能列表。
     */
    QString requestCommands();

    /**
     * 回复扩展发起的 UI 对话请求。
     */
    QString respondToExtension(const QString &id, const QJsonObject &result);

signals:
    /** 收到一条完整且合法的 Pi RPC 事件。 */
    void eventReceived(const PiEvent &event);
    /** JSONL 解析或协议处理失败。 */
    void protocolError(const QString &message);

private:
    /**
     * 累积标准输出，并严格按 LF 切分 JSONL 记录。
     */
    void consumeOutput(const QByteArray &data);

    /** 按时间片分派积压事件，及时让出 UI 线程处理输入和绘制。 */
    void drainOutput();

    /**
     * 解析一条完整 JSON 记录。
     */
    void parseLine(QByteArray line);

    PiProcess *m_process;
    QByteArray m_buffer;
    QTimer m_dispatchTimer;
    quint64 m_nextRequestId = 1;
    int m_parseErrorCount = 0;
};
