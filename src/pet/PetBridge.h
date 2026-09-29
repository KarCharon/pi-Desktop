#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QUrl>

/**
 * 解析桌宠 helper 的 JSONL 请求，并通过受认证的本机 HTTP 回调返回响应。
 */
class PetBridge final : public QObject
{
    Q_OBJECT

public:
    /** 创建不绑定进程的桌宠桥接器。 */
    explicit PetBridge(QObject *parent = nullptr);

    /** 将桥接器绑定到本次启动的 helper 进程和随机认证令牌。 */
    void attach(QProcess *process, quint64 generation, const QByteArray &token);

    /** 解除当前进程绑定并使旧请求全部失效。 */
    void detach();

    /** 返回当前桥接代次。 */
    quint64 generation() const;

    /** 返回当前是否已经收到可处理的桥接请求。 */
    bool active() const;

    /** 向当前请求的 cb 地址发送 JSON 响应。 */
    void respondJson(const QJsonObject &request, int status, const QJsonObject &body);

    /** 向当前请求的 cb 地址发送受白名单保护的本地文件响应。 */
    void respondFile(const QJsonObject &request, int status, const QString &contentType,
                     const QString &filePath);

    /** 向当前请求返回稳定的错误 JSON。 */
    void respondError(const QJsonObject &request, int status, const QString &code,
                      const QString &message);

signals:
    /** 收到经过信封校验的请求，交由 PetController 路由。 */
    void requestReceived(const QJsonObject &request);

    /** 协议错误达到需要停止桌宠的程度。 */
    void fatalError(const QString &message);

    /** 非致命桥接诊断摘要。 */
    void diagnostic(const QString &message);

    /** 当前请求已经完成 HTTP 回调。 */
    void responseCompleted(const QString &id, bool success);

private:
    /** 消费 helper 标准输出并按 LF 拆分 JSONL。 */
    void consumeOutput();

    /** 校验单条请求并发出路由信号。 */
    void parseLine(QByteArray line);

    /** 校验回调地址只能指向绑定代次的本机端点。 */
    bool validateCallback(const QString &callback);

    /** 发送带认证头且禁止代理、重定向的 HTTP 回调。 */
    void postResponse(const QJsonObject &request, const QByteArray &body,
                      const QString &contentType, const QString &filePath = {});

    /** 将 JSON id 规范化为待处理集合使用的键。 */
    static QString requestIdKey(const QJsonValue &id);

    QPointer<QProcess> m_process; ///< 当前桌宠 helper 进程，仅弱引用避免析构顺序问题。
    QNetworkAccessManager m_network; ///< 仅用于本机回调，始终禁用代理。
    QByteArray m_buffer; ///< 未完成 JSONL 行的 UTF-8 缓冲区。
    QByteArray m_token; ///< 当前进程代次的认证令牌，仅驻留内存。
    QUrl m_callbackUrl; ///< 首个合法回调地址，运行期间禁止切换。
    QSet<QString> m_pendingIds; ///< 当前代次已经接收且尚未回调的请求 id。
    quint64 m_generation = 0; ///< 进程启动代次，用于丢弃旧连接结果。
    bool m_active = false; ///< 当前是否已经绑定 helper。
};
