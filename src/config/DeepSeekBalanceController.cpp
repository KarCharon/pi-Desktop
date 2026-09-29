#include "DeepSeekBalanceController.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDebug>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

/** 限制金额长度和小数精度，避免恶意字符串和浮点误差。 */
static bool validAmount(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{1,24}(\\.[0-9]{1,18})?$"));
    return pattern.match(value).hasMatch();
}

/** 将十进制字符串提升到相同精度并去掉无意义前导零。 */
static QString scaledDigits(QString value, int scale)
{
    const int point = value.indexOf(u'.');
    const int decimals = point < 0 ? 0 : value.size() - point - 1;
    value.remove(u'.');
    value += QString(scale - decimals, u'0');
    while (value.size() > 1 && value.front() == u'0')
        value.remove(0, 1);
    return value;
}

/** 创建强制直连的请求服务与独立总截止定时器。 */
DeepSeekBalanceController::DeepSeekBalanceController(QObject *parent, RequestFactory factory,
                                                     bool diagnosticsEnabled, const QString &diagnosticDirectory)
    : QObject(parent), m_factory(std::move(factory))
{
    m_logClock.start();
    m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (diagnosticsEnabled && (!m_factory || !diagnosticDirectory.isEmpty())) {
        const QString directory = diagnosticDirectory.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("logs"))
            : diagnosticDirectory;
        if (QDir().mkpath(directory))
            m_logPath = QDir(directory).filePath(QStringLiteral("deepseek-balance.jsonl"));
        else
            qWarning("[DeepSeekBalance] Cannot create diagnostic directory");
    }
    trace(QStringLiteral("controller_started"), {{"schema", 1}, {"transport", m_factory ? "test" : "direct"}});
    m_network.setProxy(QNetworkProxy::NoProxy);
    for (QTimer *timer : {&m_schedule, &m_retry, &m_deadline})
        timer->setSingleShot(true);
    m_poll.setInterval(5 * 60 * 1000);
    m_poll.setTimerType(Qt::PreciseTimer);
    // 周期触发仍复用合并与冷却逻辑，不绕过 429 限制。
    connect(&m_poll, &QTimer::timeout, this, [this] {
        requestRefresh(QStringLiteral("periodic"));
    });
    m_deadline.setTimerType(Qt::PreciseTimer);
    connect(&m_schedule, &QTimer::timeout, this, &DeepSeekBalanceController::begin);
    connect(&m_retry, &QTimer::timeout, this, &DeepSeekBalanceController::attempt);
    // 总时限包括连接与响应下载，不能被持续到来的字节重置。
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        m_timeout = true;
        trace(QStringLiteral("request_timeout"), {{"request_elapsed_ms", m_attemptClock.elapsed()}});
        if (m_reply)
            m_reply->abort();
    });
}

/** 先断开 reply 回调再释放成员，退出不触发补查。 */
DeepSeekBalanceController::~DeepSeekBalanceController()
{
    cancel();
}

/** 仅记录余额专用诊断，不接管全局 Qt 日志或记录原始网络数据。 */
void DeepSeekBalanceController::trace(const QString &event, QJsonObject fields)
{
    if (m_logPath.isEmpty())
        return;
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    fields.insert(QStringLiteral("elapsed_ms"), m_logClock.elapsed());
    fields.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    fields.insert(QStringLiteral("run"), m_runId);
    fields.insert(QStringLiteral("sequence"), qint64(++m_eventId));
    fields.insert(QStringLiteral("task"), qint64(m_taskId));
    fields.insert(QStringLiteral("generation"), qint64(m_generation));
    fields.insert(QStringLiteral("retry"), m_retries);
    fields.insert(QStringLiteral("enabled"), m_enabled);
    fields.insert(QStringLiteral("state"), int(m_state));
    fields.insert(QStringLiteral("active"), m_active);
    fields.insert(QStringLiteral("pending"), m_pending);
    const QByteArray line = QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
    // 多开实例不交叉写入或同时轮转；锁竞争时不阻塞聊天线程。
    QLockFile lock(m_logPath + QStringLiteral(".lock"));
    bool success = lock.tryLock(0);
    if (success) {
        QFile file(m_logPath);
        if (file.size() + line.size() > 1024 * 1024) {
            const QString backup = m_logPath + QStringLiteral(".1");
            success = (!QFile::exists(backup) || QFile::remove(backup)) && file.rename(backup);
            file.setFileName(m_logPath);
        }
        if (success)
            success = file.open(QIODevice::WriteOnly | QIODevice::Append)
                && file.write(line) == line.size() && file.flush();
    }
    if (!success && !m_logWarningIssued) {
        m_logWarningIssued = true;
        qWarning("[DeepSeekBalance] Diagnostic write skipped (busy or unavailable)");
    }
}

/** 模型通知仅记录是否匹配，不把 provider 原始文本或聊天数据写入日志。 */
void DeepSeekBalanceController::modelResponseCompleted(bool deepseek, const QString &source)
{
    const QString safeSource = source == QStringLiteral("compaction_end") ? source : QStringLiteral("assistant_message_end");
    trace(QStringLiteral("model_response"), {{"source", safeSource}, {"deepseek", deepseek},
                                           {"accepted", false}, {"reason", "periodic_only"}});
}

/** 只接受固定动画阶段与数量，避免 QML 成为任意敏感文本日志入口。 */
void DeepSeekBalanceController::traceAnimation(const QString &stage, int count)
{
    const QStringList allowed{QStringLiteral("received"), QStringLiteral("skip_hidden"),
        QStringLiteral("skip_no_layer"), QStringLiteral("created"), QStringLiteral("create_failed"),
        QStringLiteral("finished"), QStringLiteral("cleared"), QStringLiteral("capacity_drop")};
    if (allowed.contains(stage))
        trace(QStringLiteral("animation_") + stage, {{"count", std::clamp(count, 0, 3)}});
}

/** 使所有旧响应失效并清理密钥、请求与重试任务。 */
void DeepSeekBalanceController::cancel()
{
    trace(QStringLiteral("context_cancel"), {{"in_flight", !m_reply.isNull()},
        {"scheduled", m_schedule.isActive()}, {"retry_scheduled", m_retry.isActive()}});
    ++m_generation;
    m_poll.stop();
    m_schedule.stop();
    m_retry.stop();
    m_deadline.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_key.fill('\0');
    m_key.clear();
    m_active = m_pending = false;
}

/** 上下文改变时清空旧账户基线，启用后首次查询并启动五分钟周期。 */
void DeepSeekBalanceController::setContext(const QString &profile, bool enabled)
{
    if (m_profile == profile && m_enabled == enabled)
        return;
    trace(QStringLiteral("context_change"), {{"profile_changed", m_profile != profile}, {"next_enabled", enabled}});
    cancel();
    m_profile = profile;
    m_enabled = enabled;
    m_balances.clear();
    m_identity.clear();
    m_available = false;
    m_notBefore = 0;
    m_state = enabled ? Loading : Disabled;
    m_reason = enabled ? QStringLiteral("loading") : QStringLiteral("disabled");
    m_updatedAt = {};
    m_text = enabled ? tr("DeepSeek: 获取余额…") : QString();
    m_status.clear();
    emit contextReset();
    emit changed();
    if (enabled) {
        m_poll.start();
        requestRefresh(QStringLiteral("context_enabled"));
    }
}

/** 设置未启用状态的脱敏原因，不读取凭据也不触发请求。 */
void DeepSeekBalanceController::setUnavailableReason(const QString &reason)
{
    if (m_enabled || m_reason == reason)
        return;
    m_reason = reason;
    m_status = reason == QStringLiteral("provider_unsupported") ? tr("当前模型不支持余额查询")
        : reason == QStringLiteral("not_connected") ? tr("Pi 尚未连接")
        : reason == QStringLiteral("disabled") ? tr("余额查询已关闭") : tr("余额暂不可用");
    m_updatedAt = QDateTime::currentDateTime();
    emit changed();
}

/** 返回不含密钥的结构化余额快照。 */
QVariantMap DeepSeekBalanceController::snapshot() const
{
    QVariantMap values;
    QVariantMap balances;
    for (auto it = m_balances.cbegin(); it != m_balances.cend(); ++it)
        balances.insert(it.key(), it.value());
    const QString stateName = m_state == Disabled ? QStringLiteral("disabled")
        : m_state == Loading ? QStringLiteral("loading")
        : m_state == Ready ? QStringLiteral("ready") : QStringLiteral("error");
    values.insert(QStringLiteral("state"), stateName);
    values.insert(QStringLiteral("available"), m_available);
    values.insert(QStringLiteral("reason"), m_reason);
    values.insert(QStringLiteral("status"), m_status);
    values.insert(QStringLiteral("display"), m_text);
    values.insert(QStringLiteral("balances"), balances);
    values.insert(QStringLiteral("updatedAt"), m_updatedAt.isValid()
                      ? m_updatedAt.toString(Qt::ISODateWithMs) : QString());
    return values;
}

/** 合并并发触发并尊重冷却时间，不允许手动绕过限流。 */
void DeepSeekBalanceController::refresh()
{
    requestRefresh(QStringLiteral("manual"));
}

/** 记录禁用、合并及冷却分支，保留原有单并发刷新语义。 */
void DeepSeekBalanceController::requestRefresh(const QString &source)
{
    trace(QStringLiteral("refresh_trigger"), {{"source", source}});
    if (!m_enabled) {
        trace(QStringLiteral("refresh_ignored_disabled"));
        return;
    }
    if (m_active) {
        m_pending = true;
        trace(QStringLiteral("refresh_merged_active"));
        return;
    }
    if (!m_schedule.isActive()) {
        const qint64 delay = std::max<qint64>(200, m_notBefore - QDateTime::currentMSecsSinceEpoch());
        trace(QStringLiteral("refresh_scheduled"), {{"delay_ms", delay}});
        m_schedule.start(int(std::min<qint64>(delay, std::numeric_limits<int>::max())));
    } else {
        trace(QStringLiteral("refresh_merged_scheduled"));
    }
}

/** 从当前 Profile 取得直接密钥；身份变化时重建余额基线。 */
void DeepSeekBalanceController::begin()
{
    if (!m_enabled)
        return;
    if (QDateTime::currentMSecsSinceEpoch() < m_notBefore) {
        requestRefresh(QStringLiteral("cooldown"));
        return;
    }
    ++m_taskId;
    m_active = true;
    m_pending = false;
    m_retries = 0;
    trace(QStringLiteral("task_begin"));
    QFile file(QDir(m_profile).filePath(QStringLiteral("auth.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        fail(tr("当前 Profile 的凭据不可读"), QStringLiteral("credentials_missing"));
        complete();
        return;
    }
    const auto auth = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("deepseek")).toObject();
    const QString key = auth.value(QStringLiteral("key")).toString();
    // 不执行命令型凭据，不把控制字符或环境表达式作为认证头发送。
    if (auth.value(QStringLiteral("type")).toString() != QStringLiteral("api_key")
        || !key.startsWith(QStringLiteral("sk-")) || key.size() > 4096
        || key.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f\\x7f]")))) {
        fail(tr("当前 Profile 未配置可用的 DeepSeek API Key"), QStringLiteral("credentials_invalid"));
        complete();
        return;
    }
    m_key = key.toUtf8();
    const auto identity = QCryptographicHash::hash(m_key, QCryptographicHash::Sha256);
    if (identity != m_identity) {
        trace(QStringLiteral("baseline_reset"), {{"reason", m_identity.isEmpty() ? "first_identity" : "identity_changed"}});
        m_balances.clear();
        m_identity = identity;
        emit contextReset();
    }
    m_state = Loading;
    m_text = tr("DeepSeek: 获取余额…");
    m_status = tr("正在直连查询官方账户余额");
    emit changed();
    attempt();
}

/** 单次请求固定官方地址且拒绝重定向，1 秒内未完成就中止。 */
void DeepSeekBalanceController::attempt()
{
    // 长 Retry-After 分段等待，不能因 QTimer 的整型区间限制而提前重试。
    const qint64 remaining = m_notBefore - QDateTime::currentMSecsSinceEpoch();
    if (remaining > 0) {
        trace(QStringLiteral("request_wait_cooldown"), {{"delay_ms", remaining}});
        m_retry.start(int(std::min<qint64>(remaining, std::numeric_limits<int>::max())));
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.deepseek.com/user/balance")));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + m_key);
    request.setRawHeader("Accept", "application/json");
    m_timeout = m_oversized = false;
    m_body.clear();
    trace(QStringLiteral("request_start"), {{"timeout_ms", 1000}, {"proxy", "none"}});
    m_attemptClock.start();
    m_reply = m_factory ? m_factory(request) : m_network.get(request);
    if (!m_reply) {
        fail(tr("无法创建余额请求"), QStringLiteral("request_create_failed"));
        complete();
        return;
    }
    auto *reply = m_reply.data();
    const auto generation = m_generation;
    // 分段消费并限制累计数据，避免错误服务返回无限响应。
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        m_body += reply->read(65537 - m_body.size());
        if (m_body.size() > 65536 || reply->bytesAvailable() > 0) {
            m_oversized = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        finish(reply, generation);
    });
    m_deadline.start(1000);
}

/** 校验完整响应，更新精确差额，或按有限预算安排临时故障重试。 */
void DeepSeekBalanceController::finish(QNetworkReply *reply, quint64 generation)
{
    if (generation != m_generation || reply != m_reply) {
        trace(QStringLiteral("response_discarded"));
        return;
    }
    m_deadline.stop();
    m_body += reply->read(65537 - m_body.size());
    m_oversized = m_oversized || m_body.size() > 65536 || reply->bytesAvailable() > 0;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    trace(QStringLiteral("response_received"), {{"http_status", code}, {"network_error", int(error)},
        {"request_elapsed_ms", m_attemptClock.elapsed()}, {"bytes", m_body.size()},
        {"timeout", m_timeout}, {"oversized", m_oversized}});
    const QByteArray retryAfter = reply->rawHeader("Retry-After");
    reply->deleteLater();
    m_reply = nullptr;
    QMap<QString, QString> balances;
    bool available = false;
    if (!m_timeout && !m_oversized && code == 200 && error == QNetworkReply::NoError
        && parseBalance(m_body, balances, available)) {
        trace(QStringLiteral("response_valid"), {{"currencies", balances.size()}, {"available", available}});
        const auto previous = m_balances;
        m_balances = balances;
        m_available = available;
        m_state = Ready;
        m_reason = available ? QStringLiteral("available") : QStringLiteral("account_unavailable");
        m_updatedAt = QDateTime::currentDateTime();
        QStringList texts;
        for (auto it = balances.cbegin(); it != balances.cend(); ++it)
            texts << QStringLiteral("💵 %1%2").arg(currencySymbol(it.key()), it.value());
        m_text = QStringLiteral("DeepSeek: ") + texts.join(QStringLiteral(" · "));
        m_status = (available ? tr("官方账户余额") : tr("余额不足 / 账户不可用"))
            + tr("；更新于 %1；浮字表示账户余额变化，并非单次请求费用")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        emit changed();
        for (auto it = balances.cbegin(); it != balances.cend(); ++it) {
            const auto delta = decrease(previous.value(it.key()), it.value());
            const QString outcome = !previous.contains(it.key()) ? QStringLiteral("baseline")
                : !delta.isEmpty() ? QStringLiteral("decreased")
                : scaledDigits(previous.value(it.key()), 18) == scaledDigits(it.value(), 18)
                    ? QStringLiteral("unchanged") : QStringLiteral("increased");
            trace(QStringLiteral("balance_compare"), {{"currency", it.key()}, {"previous", previous.value(it.key())},
                {"current", it.value()}, {"delta", delta}, {"outcome", outcome}});
            if (!delta.isEmpty()) {
                trace(QStringLiteral("decrease_signal"), {{"currency", it.key()}, {"delta", delta}});
                emit balanceDecreased(it.key(), delta);
            }
        }
        complete();
        return;
    }
    const bool transient = !m_oversized && (m_timeout || code == 429 || code >= 500
        || (code == 0 && error != QNetworkReply::SslHandshakeFailedError
            && error != QNetworkReply::OperationCanceledError));
    qint64 delay = (1 << m_retries) * 1000;
    if (code == 429) {
        bool ok = false;
        const qint64 seconds = retryAfter.toLongLong(&ok);
        const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
        delay = ok && seconds >= 0 && seconds <= 2147483 ? seconds * 1000
            : date.isValid() ? std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(date)) : 30000;
        delay = std::max<qint64>(1000, delay);
        m_notBefore = QDateTime::currentMSecsSinceEpoch() + delay;
    }
    const QString failureReason = m_timeout ? tr("请求超过 1 秒") : m_oversized ? tr("余额响应过大")
         : code == 401 ? tr("当前 Profile 的密钥认证失败")
         : code == 200 ? tr("余额响应格式异常") : tr("网络或服务错误（HTTP %1）").arg(code);
    const QString failureCode = m_timeout ? QStringLiteral("timeout") : m_oversized ? QStringLiteral("response_too_large")
         : code == 401 ? QStringLiteral("credentials_rejected")
         : code == 200 ? QStringLiteral("response_invalid") : QStringLiteral("network_error");
    fail(failureReason, failureCode);
    if (transient && m_retries < 3) {
        ++m_retries;
        m_status += tr("；等待重试 %1/3").arg(m_retries);
        emit changed();
        trace(QStringLiteral("retry_scheduled"), {{"delay_ms", delay}});
        m_retry.start(int(std::min<qint64>(delay, std::numeric_limits<int>::max())));
    } else {
        complete();
    }
}

/** 统一展示失败状态，原因只能来自本地脱敏文案。 */
void DeepSeekBalanceController::fail(const QString &reason, const QString &code)
{
    m_state = Error;
    m_reason = code.isEmpty() ? QStringLiteral("query_failed") : code;
    m_updatedAt = QDateTime::currentDateTime();
    m_text = tr("DeepSeek: 余额获取失败");
    m_status = reason;
    trace(QStringLiteral("query_failed"), {{"reason", reason}});
    emit changed();
}

/** 结束任务并清除密钥；只保留一个合并后的后续请求。 */
void DeepSeekBalanceController::complete()
{
    trace(QStringLiteral("task_complete"));
    m_key.fill('\0');
    m_key.clear();
    m_active = false;
    m_notBefore = std::max(m_notBefore, QDateTime::currentMSecsSinceEpoch() + 1000);
    if (m_pending) {
        m_pending = false;
        requestRefresh(QStringLiteral("pending"));
    }
}

/** 全量验证币种与金额，不允许部分有效响应覆盖上次余额。 */
bool DeepSeekBalanceController::parseBalance(const QByteArray &json, QMap<QString, QString> &balances, bool &available)
{
    balances.clear();
    const auto object = QJsonDocument::fromJson(json).object();
    if (json.size() > 65536 || !object.value(QStringLiteral("is_available")).isBool()
        || !object.value(QStringLiteral("balance_infos")).isArray())
        return false;
    const auto infos = object.value(QStringLiteral("balance_infos")).toArray();
    if (infos.isEmpty() || infos.size() > 16)
        return false;
    QMap<QString, QString> result;
    for (const auto &value : infos) {
        const auto info = value.toObject();
        const auto currency = info.value(QStringLiteral("currency")).toString();
        if (!QRegularExpression(QStringLiteral("^[A-Z]{3}$")).match(currency).hasMatch() || result.contains(currency))
            return false;
        for (const auto &field : {"total_balance", "granted_balance", "topped_up_balance"}) {
            if (!info.value(QLatin1String(field)).isString() || !validAmount(info.value(QLatin1String(field)).toString()))
                return false;
        }
        result.insert(currency, info.value(QStringLiteral("total_balance")).toString());
    }
    balances = result;
    available = object.value(QStringLiteral("is_available")).toBool();
    return true;
}

/** 按十进制位执行借位减法，保留至两次观测的最大小数精度。 */
QString DeepSeekBalanceController::decrease(const QString &oldValue, const QString &newValue)
{
    if (!validAmount(oldValue) || !validAmount(newValue))
        return {};
    const int scale = std::max(oldValue.contains(u'.') ? oldValue.size() - oldValue.indexOf(u'.') - 1 : 0,
                               newValue.contains(u'.') ? newValue.size() - newValue.indexOf(u'.') - 1 : 0);
    auto oldDigits = scaledDigits(oldValue, scale);
    auto newDigits = scaledDigits(newValue, scale);
    if (oldDigits.size() < newDigits.size() || (oldDigits.size() == newDigits.size() && oldDigits <= newDigits))
        return {};
    newDigits = newDigits.rightJustified(oldDigits.size(), u'0');
    int borrow = 0;
    for (qsizetype i = oldDigits.size(); i-- > 0;) {
        int digit = oldDigits[i].digitValue() - newDigits[i].digitValue() - borrow;
        borrow = digit < 0 ? 1 : 0;
        oldDigits[i] = QChar(u'0' + digit + borrow * 10);
    }
    while (oldDigits.size() > scale + 1 && oldDigits.front() == u'0')
        oldDigits.remove(0, 1);
    oldDigits = oldDigits.rightJustified(scale + 1, u'0');
    if (scale > 0)
        oldDigits.insert(oldDigits.size() - scale, u'.');
    return oldDigits;
}

/** 人民币使用 ¥，美元使用 $，未知币种显示原代码以避免误标。 */
QString DeepSeekBalanceController::currencySymbol(const QString &currency)
{
    return currency == QStringLiteral("CNY") ? QStringLiteral("¥")
        : currency == QStringLiteral("USD") ? QStringLiteral("$") : currency + u' ';
}
