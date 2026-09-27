#include "config/DeepSeekBalanceController.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

/** 内存网络响应；支持挂起、HTTP 错误与取消，绝不访问网络。 */
class BalanceReply final : public QNetworkReply
{
public:
    /** delay<0 时保持挂起以验证总截止时间，否则异步发布完整响应。 */
    BalanceReply(const QNetworkRequest &request, QByteArray body, int code = 200, int delay = 0,
                 const QByteArray &retryAfter = {})
        : m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
        if (!retryAfter.isEmpty()) setRawHeader("Retry-After", retryAfter);
        open(QIODevice::ReadOnly);
        if (delay >= 0) {
            QTimer::singleShot(delay, this, [this] {
                if (isFinished()) return;
                emit readyRead();
                setFinished(true);
                emit finished();
            });
        }
    }
    /** 模拟真实 reply 的取消完成信号。 */
    void abort() override
    {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }
    /** 返回剩余内存数据大小。 */
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    /** 按调用者的长度上限消费测试数据。 */
    qint64 readData(char *data, qint64 size) override
    {
        const qint64 length = qMin(size, m_body.size() - m_offset);
        if (!length) return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(length));
        m_offset += length;
        return length;
    }
private:
    QByteArray m_body; ///< 完整桩响应，不含真实数据。
    qint64 m_offset = 0; ///< 已读取字节数。
};

/** 覆盖凭据边界、精确差额、隔离及有限重试。 */
class DeepSeekBalanceTest final : public QObject
{
    Q_OBJECT
    /** 生成符合官方结构的单币种样例。 */
    static QByteArray body(const QString &amount, bool available = true)
    {
        return QJsonDocument(QJsonObject{{"is_available", available}, {"balance_infos", QJsonArray{
            QJsonObject{{"currency", "CNY"}, {"total_balance", amount},
                        {"granted_balance", "0.00"}, {"topped_up_balance", amount}}}}}).toJson();
    }
    /** 在临时 Profile 中写入纯测试凭据。 */
    static void auth(const QString &path, const QString &key = QStringLiteral("sk-test-only"))
    {
        QFile file(path + "/auth.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"deepseek", QJsonObject{{"type", "api_key"}, {"key", key}}}}).toJson());
    }
private slots:
    /** 五分钟轮询仅在启用时运行；模型事件不查询，禁用及换上下文清除旧周期。 */
    void periodicRefreshLifecycle()
    {
        QTemporaryDir profile;
        auth(profile.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, body("110.00"));
        });
        QCOMPARE(controller.m_poll.interval(), 300000);
        QVERIFY(!controller.m_poll.isSingleShot());
        QVERIFY(!controller.m_poll.isActive());
        controller.setContext(profile.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        QCOMPARE(calls, 1);
        QVERIFY(controller.m_poll.isActive());
        controller.modelResponseCompleted(true, QStringLiteral("assistant_message_end"));
        controller.modelResponseCompleted(true, QStringLiteral("compaction_end"));
        QTest::qWait(1200);
        QCOMPARE(calls, 1);
        // 模拟周期到期，验证实际连接而不是等待五分钟。
        QVERIFY(QMetaObject::invokeMethod(&controller.m_poll, "timeout", Qt::DirectConnection));
        QTRY_COMPARE(calls, 2);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        controller.refresh();
        QTRY_COMPARE(calls, 3);
        controller.setContext(profile.path(), false);
        QVERIFY(!controller.m_poll.isActive());
        QVERIFY(QMetaObject::invokeMethod(&controller.m_poll, "timeout", Qt::DirectConnection));
        QTest::qWait(1200);
        QCOMPARE(calls, 3);
        controller.setContext(profile.path(), true);
        QVERIFY(controller.m_poll.isActive());
        QTRY_COMPARE(calls, 4);
        QTemporaryDir other;
        auth(other.path());
        controller.setContext(other.path(), true);
        QVERIFY(controller.m_poll.isActive());
        QTRY_COMPARE(calls, 5);
    }

    /** 诊断只包含白名单元数据，记录余额比较但不泄露密钥、路径或原始响应。 */
    void diagnosticPrivacyAndRotation()
    {
        QTemporaryDir profile;
        QTemporaryDir logs;
        const QString secret = QStringLiteral("sk-test-secret-never-log");
        auth(profile.path(), secret);
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, calls < 3 ? body(calls == 1 ? "110.00" : "109.98")
                                                      : QByteArray("raw-private-response-marker"));
        }, true, logs.path());
        controller.setContext(profile.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        controller.modelResponseCompleted(true, QStringLiteral("assistant_message_end"));
        QVERIFY(QMetaObject::invokeMethod(&controller.m_poll, "timeout", Qt::DirectConnection));
        QTRY_COMPARE(calls, 2);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        controller.traceAnimation(QStringLiteral("created"), 1);
        controller.traceAnimation(secret, 100); // 未知阶段不进入日志。
        controller.refresh();
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Error);
        QFile log(controller.diagnosticLogPath());
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto content = log.readAll();
        log.close();
        QVERIFY(!content.contains(secret.toUtf8()));
        QVERIFY(!content.contains("Authorization"));
        QVERIFY(!content.contains("raw-private-response-marker"));
        QVERIFY(!content.contains(profile.path().toUtf8()));
        for (const auto &event : {"model_response", "refresh_trigger", "request_start", "response_received",
                                 "balance_compare", "decrease_signal", "animation_created", "query_failed"})
            QVERIFY2(content.contains(event), event);
        QVERIFY(content.contains("unchanged") == false);
        QVERIFY(content.contains("decreased"));
        for (const auto &line : content.split('\n')) {
            if (line.isEmpty()) continue;
            const auto object = QJsonDocument::fromJson(line).object();
            QVERIFY(!object.isEmpty());
            QVERIFY(object.contains("run") && object.contains("task") && object.contains("generation"));
        }
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(log.write(QByteArray(1024 * 1024, 'x')), qint64(1024 * 1024));
        log.close();
        controller.traceAnimation(QStringLiteral("finished"), 0);
        QVERIFY(QFile::exists(controller.diagnosticLogPath() + ".1"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto rotated = log.readAll();
        QVERIFY(rotated.size() < 2048);
        QVERIFY(rotated.contains("animation_finished"));
    }

    /** 金额精度不依赖浮点或固定两位小数。 */
    void exactDecimals()
    {
        QCOMPARE(DeepSeekBalanceController::decrease("110.00", "109.98"), "0.02");
        QCOMPARE(DeepSeekBalanceController::decrease("1", "0.999999999999999999"), "0.000000000000000001");
        QCOMPARE(DeepSeekBalanceController::decrease("0002.00", "1.999"), "0.001");
        QVERIFY(DeepSeekBalanceController::decrease("1.0", "1.00").isEmpty());
        QVERIFY(DeepSeekBalanceController::decrease("1", "2").isEmpty());
        QVERIFY(DeepSeekBalanceController::decrease("", "0").isEmpty());
        QCOMPARE(DeepSeekBalanceController::currencySymbol("CNY"), QString::fromUtf8("¥"));
    }
    /** 错误类型、空数组、重复币种和非十进制金额不能被当作零余额。 */
    void validation()
    {
        QMap<QString, QString> balances;
        bool available = true;
        QVERIFY(DeepSeekBalanceController::parseBalance(body("0.00", false), balances, available));
        QVERIFY(!available);
        QVERIFY(!DeepSeekBalanceController::parseBalance("{}", balances, available));
        QVERIFY(!DeepSeekBalanceController::parseBalance(body("NaN"), balances, available));
        auto object = QJsonDocument::fromJson(body("1.00")).object();
        auto infos = object["balance_infos"].toArray();
        infos.append(infos.first());
        object["balance_infos"] = infos;
        QVERIFY(!DeepSeekBalanceController::parseBalance(QJsonDocument(object).toJson(), balances, available));
        auto usd = infos.first().toObject();
        usd["currency"] = "USD";
        infos[1] = usd;
        object["balance_infos"] = infos;
        QVERIFY(DeepSeekBalanceController::parseBalance(QJsonDocument(object).toJson(), balances, available));
        QCOMPARE(balances.size(), 2);
    }
    /** 禁用时及无 Profile 凭据时不发请求，环境密钥不参与回退。 */
    void noCredentialFallback()
    {
        QTemporaryDir dir;
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &) -> QNetworkReply * {
            ++calls;
            return nullptr;
        });
        QVERIFY(controller.diagnosticLogPath().isEmpty());
        controller.setContext(dir.path(), false);
        controller.refresh();
        QTest::qWait(250);
        QCOMPARE(calls, 0);
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Error);
        QCOMPARE(calls, 0);
    }
    /** 首次读取无动画、消费产生动画、换密钥重建基线且不回退环境。 */
    void baselineAndIdentity()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        bool requestValid = true;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            requestValid &= request.url() == QUrl("https://api.deepseek.com/user/balance")
                && request.rawHeader("Authorization").startsWith("Bearer sk-")
                && request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy;
            return new BalanceReply(request, body(++calls == 1 ? "110.00" : "109.98"));
        });
        QSignalSpy spy(&controller, &DeepSeekBalanceController::balanceDecreased);
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        QCOMPARE(spy.size(), 0);
        controller.refresh();
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.first()[1].toString(), "0.02");
        QVERIFY(controller.displayText().contains(QString::fromUtf8("💵 ¥109.98")));
        auth(dir.path(), "sk-new-test");
        controller.refresh();
        QTRY_COMPARE(calls, 3);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        QCOMPARE(spy.size(), 1);
        QVERIFY(requestValid);
    }
    /** 切换非 DeepSeek 取消旧请求，不接受迟到响应。 */
    void cancellation()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, body("99"), 200, 700);
        });
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(calls, 1);
        controller.setContext(dir.path(), false);
        QTest::qWait(850);
        QCOMPARE(controller.state(), DeepSeekBalanceController::Disabled);
        QVERIFY(controller.displayText().isEmpty());
        QCOMPARE(calls, 1);
    }
    /** 401 为确定性失败，不重复发送错误认证。 */
    void unauthorizedStops()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, "{}", 401);
        });
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Error);
        QTest::qWait(1200);
        QCOMPARE(calls, 1);
    }
    /** 429 冷却不可被连续手动刷新绕过，后续成功恢复正常。 */
    void rateLimitCooldown()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return calls == 1 ? new BalanceReply(request, "{}", 429, 0, "2")
                              : new BalanceReply(request, body("100"));
        });
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Error);
        for (int i = 0; i < 10; ++i) controller.refresh();
        QTest::qWait(1200);
        QCOMPARE(calls, 1);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Ready);
        QCOMPARE(calls, 2);
        QTRY_COMPARE(calls, 3); // 十次刷新仅留下一个尾随任务。
        QTest::qWait(1200);
        QCOMPARE(calls, 3);
    }
    /** 不可信超大响应终止且不重试，不产生动画。 */
    void oversizedResponse()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, QByteArray(70000, 'x'));
        });
        controller.setContext(dir.path(), true);
        QTRY_COMPARE(controller.state(), DeepSeekBalanceController::Error);
        QTest::qWait(1200);
        QCOMPARE(calls, 1);
    }
    /** 永远不响应的请求每次 1 秒截止，首次加三次重试后停止。 */
    void timeoutBudget()
    {
        QTemporaryDir dir;
        auth(dir.path());
        int calls = 0;
        DeepSeekBalanceController controller(nullptr, [&](const QNetworkRequest &request) {
            ++calls;
            return new BalanceReply(request, {}, 0, -1);
        });
        controller.setContext(dir.path(), true);
        QTRY_COMPARE_WITH_TIMEOUT(calls, 4, 14000);
        QTest::qWait(2200);
        QCOMPARE(calls, 4);
        QCOMPARE(controller.state(), DeepSeekBalanceController::Error);
    }
};

QTEST_GUILESS_MAIN(DeepSeekBalanceTest)
#include "DeepSeekBalanceTest.moc"
