#include "PetBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRegularExpression>

namespace {
const QByteArray kPrefix = "dsh-pet-bridge:";
constexpr qsizetype kMaximumLineBytes = 256 * 1024;
constexpr qsizetype kMaximumBufferBytes = 512 * 1024;
constexpr int kMaximumPendingRequests = 64;
constexpr qsizetype kMaximumFileBytes = 64 * 1024 * 1024;
}

Q_LOGGING_CATEGORY(petBridgeLog, "pidesktop.pet.bridge")

/** 创建桌宠桥接器并固定本机回调网络策略。 */
PetBridge::PetBridge(QObject *parent)
    : QObject(parent)
{
    m_network.setProxy(QNetworkProxy::NoProxy);
}

/** 绑定一个新的 helper 代次，旧缓冲和请求不能跨进程复用。 */
void PetBridge::attach(QProcess *process, quint64 generation, const QByteArray &token)
{
    detach();
    m_process = process;
    m_generation = generation;
    m_token = token;
    m_active = process != nullptr && !token.isEmpty();
    if (!m_active)
        return;
    connect(process, &QProcess::readyReadStandardOutput, this, &PetBridge::consumeOutput);
    qCInfo(petBridgeLog) << "[PetBridge] 已绑定 helper；generation=" << generation;
}

/** 断开旧 helper 并清除认证令牌、回调地址和待处理请求。 */
void PetBridge::detach()
{
    if (m_process)
        disconnect(m_process, nullptr, this, nullptr);
    m_process = nullptr;
    m_buffer.clear();
    m_token.fill('\0');
    m_token.clear();
    m_callbackUrl.clear();
    m_pendingIds.clear();
    m_active = false;
}

/** 返回当前桥接代次。 */
quint64 PetBridge::generation() const
{
    return m_generation;
}

/** 返回桥接是否仍绑定有效 helper。 */
bool PetBridge::active() const
{
    return m_active && !m_process.isNull();
}

/** 消费标准输出，忽略普通诊断，只处理固定前缀的 JSONL 行。 */
void PetBridge::consumeOutput()
{
    if (!active())
        return;
    m_buffer += m_process->readAllStandardOutput();
    if (m_buffer.size() > kMaximumBufferBytes) {
        qCWarning(petBridgeLog) << "[PetBridge] 协议缓冲区超限；bytes=" << m_buffer.size();
        emit fatalError(tr("桌宠桥接协议缓冲区过大。"));
        return;
    }

    while (true) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            if (m_buffer.size() > kMaximumLineBytes) {
                qCWarning(petBridgeLog) << "[PetBridge] 未完成协议行超限；bytes=" << m_buffer.size();
                emit fatalError(tr("桌宠桥接协议行过长。"));
            }
            return;
        }
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        parseLine(std::move(line));
        if (!active())
            return;
    }
}

/** 校验 envelope、回调端点和请求并发上限后交给控制器路由。 */
void PetBridge::parseLine(QByteArray line)
{
    if (!line.startsWith(kPrefix))
        return;
    line.remove(0, kPrefix.size());
    if (line.size() > kMaximumLineBytes) {
        emit fatalError(tr("桌宠桥接请求过大。"));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (!document.isObject()) {
        qCWarning(petBridgeLog) << "[PetBridge] 非法 JSON 请求";
        emit fatalError(tr("桌宠桥接请求不是合法 JSON。"));
        return;
    }
    const QJsonObject request = document.object();
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    const QString callback = request.value(QStringLiteral("cb")).toString();
    const QString url = request.value(QStringLiteral("url")).toString();
    const QJsonValue requestBody = request.value(QStringLiteral("body"));
    const qsizetype bodyBytes = requestBody.isString() ? requestBody.toString().toUtf8().size()
        : requestBody.isObject() ? QJsonDocument(requestBody.toObject()).toJson(QJsonDocument::Compact).size()
        : requestBody.isArray() ? QJsonDocument(requestBody.toArray()).toJson(QJsonDocument::Compact).size() : 0;
    if ((id.isString() && id.toString().size() > 128)
        || (id.isDouble() && !qIsFinite(id.toDouble()))
        || (!id.isString() && !id.isDouble())
        || (method != QStringLiteral("GET") && method != QStringLiteral("POST"))
        || callback.size() > 2048 || url.size() > 4096 || url.isEmpty()
        || bodyBytes > 1024 * 1024 || !validateCallback(callback)) {
        qCWarning(petBridgeLog) << "[PetBridge] 请求 envelope 校验失败";
        emit fatalError(tr("桌宠桥接请求字段无效。"));
        return;
    }
    const QString idKey = requestIdKey(id);
    if (idKey.isEmpty() || m_pendingIds.contains(idKey)) {
        emit fatalError(tr("桌宠桥接请求编号重复或无效。"));
        return;
    }
    if (m_pendingIds.size() >= kMaximumPendingRequests) {
        qCWarning(petBridgeLog) << "[PetBridge] 待处理请求已达上限；limit=" << kMaximumPendingRequests;
        emit fatalError(tr("桌宠桥接请求过多。"));
        return;
    }
    m_pendingIds.insert(idKey);
    emit requestReceived(request);
}

/** 只接受本机固定回调地址，并将首个地址绑定到当前进程代次。 */
bool PetBridge::validateCallback(const QString &callback)
{
    const QUrl url(callback, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("http")
        || url.host() != QStringLiteral("127.0.0.1") || url.port() < 1
        || url.userInfo().size() > 0 || url.hasQuery() || url.hasFragment()
        || url.path() != QStringLiteral("/respond")) {
        return false;
    }
    if (m_callbackUrl.isEmpty()) {
        m_callbackUrl = url;
        qCInfo(petBridgeLog) << "[PetBridge] 已绑定 helper 回调端点；port=" << url.port();
        return true;
    }
    return m_callbackUrl == url;
}

/** 发送回调信封，并在完成后按代次清理待处理请求。 */
void PetBridge::postResponse(const QJsonObject &request, const QByteArray &body,
                             const QString &contentType, const QString &filePath)
{
    const QString idKey = requestIdKey(request.value(QStringLiteral("id")));
    if (idKey.isEmpty() || !m_pendingIds.contains(idKey) || !active())
        return;
    const quint64 responseGeneration = m_generation;
    QJsonObject envelope{
        {QStringLiteral("id"), request.value(QStringLiteral("id"))},
        {QStringLiteral("status"), request.value(QStringLiteral("_status"))},
        {QStringLiteral("contentType"), contentType}
    };
    if (!filePath.isEmpty())
        envelope.insert(QStringLiteral("file"), filePath);
    else
        envelope.insert(QStringLiteral("body"), QString::fromUtf8(body));

    QNetworkRequest callback(m_callbackUrl);
    callback.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::ManualRedirectPolicy);
    callback.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    callback.setRawHeader("X-DSH-PET-TOKEN", m_token);
    const QByteArray payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    auto *reply = m_network.post(callback, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, idKey, responseGeneration] {
        const bool current = responseGeneration == m_generation && active();
        const bool success = current && reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        m_pendingIds.remove(idKey);
        emit responseCompleted(idKey, success);
        if (!success)
            qCWarning(petBridgeLog) << "[PetBridge] HTTP 回调失败；id=" << idKey
                                     << "generation=" << responseGeneration;
        reply->deleteLater();
    });
}

/** 回传序列化 JSON body，body 字段保持上游 envelope 契约。 */
void PetBridge::respondJson(const QJsonObject &request, int status, const QJsonObject &body)
{
    QJsonObject mutableRequest = request;
    mutableRequest.insert(QStringLiteral("_status"), status);
    postResponse(mutableRequest, QJsonDocument(body).toJson(QJsonDocument::Compact),
                 QStringLiteral("application/json"));
}

/** 回传经控制器验证后的文件绝对路径，不读取任意用户文件。 */
void PetBridge::respondFile(const QJsonObject &request, int status, const QString &contentType,
                            const QString &filePath)
{
    if (!QFileInfo::exists(filePath) || QFileInfo(filePath).size() > kMaximumFileBytes) {
        respondError(request, 404, QStringLiteral("asset_missing"), QStringLiteral("asset unavailable"));
        return;
    }
    QJsonObject mutableRequest = request;
    mutableRequest.insert(QStringLiteral("_status"), status);
    postResponse(mutableRequest, {}, contentType, QDir::toNativeSeparators(filePath));
}

/** 回传不包含本地路径和敏感数据的稳定错误。 */
void PetBridge::respondError(const QJsonObject &request, int status, const QString &code,
                             const QString &message)
{
    respondJson(request, status, QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), code},
        {QStringLiteral("message"), message}
    });
}

/** 规范化 JSON id，避免不同文本表示绕过重复请求校验。 */
QString PetBridge::requestIdKey(const QJsonValue &id)
{
    if (id.isString())
        return QStringLiteral("s:") + id.toString();
    if (id.isDouble() && qIsFinite(id.toDouble()))
        return QStringLiteral("n:") + QString::number(id.toDouble(), 'g', 17);
    return {};
}
