#include "agent/AgentSessionController.h"
#include "app/AppController.h"
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include "chat/ChatModel.h"
#include "pi/PiProcess.h"
#include "pi/PiRpcClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

/** 以本测试程序模拟 Pi 子进程，不访问真实模型、密钥或会话。 */
static int runFakePi()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject command = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString type = command.value("type").toString();
        QJsonObject data;
#ifdef Q_OS_WIN
        if (type == "test_console") {
            data = {{"hasConsole", GetConsoleWindow() != nullptr}};
        } else
#endif
        if (type == "test_proxy") {
            for (const QString &key : {QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
                                       QStringLiteral("ALL_PROXY"), QStringLiteral("http_proxy"),
                                       QStringLiteral("https_proxy"), QStringLiteral("all_proxy"),
                                       QStringLiteral("NO_PROXY"), QStringLiteral("no_proxy")})
                data.insert(key, qEnvironmentVariable(key.toUtf8().constData()));
        } else if (type == "get_session_stats") {
            data = {{"tokens", QJsonObject{{"total", 105000}}}, {"cost", 0.45},
                    {"contextUsage", QJsonObject{{"tokens", 60000}, {"contextWindow", 200000}, {"percent", 30}}}};
        } else if (type == "get_messages") {
            data = {{"messages", QJsonArray{}}};
        } else if (type == "get_state") {
            const QStringList arguments = QCoreApplication::arguments();
            const int sessionIndex = arguments.indexOf("--session");
            const QString session = sessionIndex < 0 ? QString() : arguments.value(sessionIndex + 1);
            data = {{"sessionFile", session}, {"sessionName", QDir::currentPath()}};
        }
        const QJsonObject response{{"type", "response"}, {"id", command.value("id")},
                                   {"command", type}, {"success", true}, {"data", data}};
        std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).constData() << std::endl;
        // 将真实管道收到的投递策略回显成队列事件，验证序列化字段而非仅验证返回值。
        if (type == "prompt") {
            const QString key = command.value("streamingBehavior").toString() == "followUp" ? "followUp" : "steering";
            const QJsonObject queued{{"type", "queue_update"}, {key, QJsonArray{command.value("message")}}};
            std::cout << QJsonDocument(queued).toJson(QJsonDocument::Compact).constData() << std::endl;
        }
    }
    return 0;
}

/** 使用真实进程管道与可控 RPC 事件验证错误、统计和忙碌状态。 */
class AgentSessionTest final : public QObject
{
    Q_OBJECT
private slots:
    void proxySettingsValidationAndPersistence()
    {
        QVERIFY(AppSettings::isValidProxyUrl(""));
        QVERIFY(AppSettings::isValidProxyUrl(" http://127.0.0.1:7890 "));
        QVERIFY(AppSettings::isValidProxyUrl("https://[::1]:8443"));
        for (const QString &bad : QStringList{"127.0.0.1:7890", "socks5://localhost:1080",
                "http://", "http://localhost:0", "http://localhost:99999",
                "http://localhost/path", "http://localhost?x=1", "http://localhost#fragment"})
            QVERIFY2(!AppSettings::isValidProxyUrl(bad), qPrintable(bad));
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const auto format = QSettings::defaultFormat();
        const auto restore = qScopeGuard([=] { QSettings::setDefaultFormat(format); });
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        AppSettings settings;
        QVERIFY(settings.proxyUrl().isEmpty());
        settings.setProxyUrl(" http://127.0.0.1:7890 ");
        QCOMPARE(AppSettings().proxyUrl(), QString("http://127.0.0.1:7890"));
        settings.setProxyUrl("socks5://localhost:1080");
        QCOMPARE(settings.proxyUrl(), QString("http://127.0.0.1:7890"));
        settings.setProxyUrl("");
        QVERIFY(AppSettings().proxyUrl().isEmpty());
    }

    /** 真实子进程检查代理覆盖、清空恢复继承、NO_PROXY 和父环境不变。 */
    void proxyEnvironmentRoundTrip()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const QByteArray key("HTTPS_PROXY");
        const bool wasSet = qEnvironmentVariableIsSet(key.constData());
        const QByteArray original = qgetenv(key.constData());
        const auto restore = qScopeGuard([=] {
            if (wasSet) qputenv(key.constData(), original);
            else qunsetenv(key.constData());
        });
        qputenv(key.constData(), "http://inherited.invalid:8080");
        const auto inherited = QProcessEnvironment::systemEnvironment();
        PiProcess process;
        process.setExecutable(QCoreApplication::applicationFilePath());
        process.setConfigDirectory(profile.path());
        process.setWorkingDirectory(profile.path());
        QByteArray output;
        connect(&process, &PiProcess::outputReceived, this,
                [&output](const QByteArray &chunk) { output += chunk; });
        for (const QString &proxy : QStringList{"http://127.0.0.1:7890", ""}) {
            process.setProxyUrl(proxy);
            output.clear();
            QVERIFY(process.start());
            QTRY_VERIFY(process.running());
            QVERIFY(process.write("{\"type\":\"test_proxy\",\"id\":\"proxy\"}\n"));
            QTRY_VERIFY(output.contains('\n'));
            const auto data = QJsonDocument::fromJson(output.left(output.indexOf('\n')))
                                  .object().value("data").toObject();
            QCOMPARE(data.size(), 8);
            for (auto it = data.begin(); it != data.end(); ++it) {
                const bool bypass = it.key().compare("NO_PROXY", Qt::CaseInsensitive) == 0;
                QCOMPARE(it.value().toString(), proxy.isEmpty() || bypass
                         ? inherited.value(it.key()) : proxy);
            }
            QCOMPARE(qgetenv(key.constData()), QByteArray("http://inherited.invalid:8080"));
            process.stop();
            QTRY_VERIFY(!process.active());
        }
    }

    void windowsLauncher_data()
    {
        QTest::addColumn<QString>("launcher");
        QTest::addColumn<QString>("cli");
        QTest::newRow("cmd-fallback") << "pi.cmd" << "";
        QTest::newRow("bat-fallback") << "pi.bat" << "";
        QTest::newRow("npm-current") << "pi.cmd" << "dist/cli.js";
        QTest::newRow("npm-bundle") << "pi.cmd" << "dist/bundle/cli.js";
    }

    /** 覆盖 npm 两种布局、CMD 引号及复用 QProcess 后的原生参数清理。 */
    void windowsLauncher()
    {
#ifdef Q_OS_WIN
        QFETCH(QString, launcher);
        QFETCH(QString, cli);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString directory = root.filePath("launcher with spaces & symbols");
        QVERIFY(QDir().mkpath(directory));
        QFile script(QDir(directory).filePath(launcher));
        QVERIFY(script.open(QIODevice::WriteOnly));
        if (cli.isEmpty()) {
            script.write((QStringLiteral("@echo off\r\n\"")
                + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                + QStringLiteral("\" %*\r\n")).toLocal8Bit());
        } else {
            // npm 路径必须直接执行 Node，而非碰巧通过脚本成功。
            script.write("@exit /b 91\r\n");
            const QString entry = QDir(directory).filePath(
                "node_modules/@earendil-works/pi-coding-agent/" + cli);
            QVERIFY(QDir().mkpath(QFileInfo(entry).absolutePath()));
            QFile stub(entry);
            QVERIFY(stub.open(QIODevice::WriteOnly));
            stub.close();
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),
                               QDir(directory).filePath("node.exe")));
        }
        script.close();
        QFile session(QDir(directory).filePath("session with spaces & symbols.jsonl"));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.close();
        PiProcess process;
        process.setConfigDirectory(root.path());
        process.setWorkingDirectory(directory);
        process.setSessionFile(session.fileName());
        QByteArray output;
        connect(&process, &PiProcess::outputReceived, this,
                [&output](const QByteArray &chunk) { output += chunk; });
        // 第二次启动走 EXE 分支，验证同一对象不会残留 CMD 参数。
        for (const QString &executable : QStringList{script.fileName(), QCoreApplication::applicationFilePath()}) {
            output.clear();
            process.setExecutable(executable);
            QVERIFY(process.start());
            QTRY_VERIFY(process.running());
            QVERIFY(process.write("{\"type\":\"get_state\",\"id\":\"launcher\"}\n"));
            QTRY_VERIFY(output.contains('\n'));
            const auto response = QJsonDocument::fromJson(output.left(output.indexOf('\n'))).object();
            QCOMPARE(response.value("id").toString(), QString("launcher"));
            const auto data = response.value("data").toObject();
            QCOMPARE(QDir::fromNativeSeparators(data.value("sessionFile").toString()), session.fileName());
            QCOMPARE(data.value("sessionName").toString(), directory);
            process.stop();
            QTRY_VERIFY(!process.active());
        }
#else
        QSKIP("仅 Windows 使用 npm/CMD 启动路径");
#endif
    }

    /** Windows 后台子进程不创建控制台，标准输入输出仍能完成 RPC 往返。 */
    void backgroundProcessHasNoConsole()
    {
#ifdef Q_OS_WIN
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        PiProcess process;
        process.setExecutable(QCoreApplication::applicationFilePath());
        process.setConfigDirectory(profile.path());
        process.setWorkingDirectory(profile.path());
        QByteArray output;
        /** 累计管道字节，避免把单次 readyRead 误当成完整响应。 */
        connect(&process, &PiProcess::outputReceived, this,
                [&output](const QByteArray &data) { output.append(data); });
        QVERIFY(process.start());
        QTRY_VERIFY(process.running());
        QVERIFY(process.write("{\"type\":\"test_console\",\"id\":\"console-check\"}\n"));
        QTRY_VERIFY(output.contains('\n'));
        const auto response = QJsonDocument::fromJson(output.left(output.indexOf('\n'))).object();
        QCOMPARE(response.value("id").toString(), QString("console-check"));
        const auto data = response.value("data").toObject();
        QVERIFY(data.contains("hasConsole"));
        QVERIFY(!data.value("hasConsole").toBool());
        process.stop();
        QTRY_VERIFY(!process.active());
#else
        QSKIP("仅 Windows 支持控制台窗口检查");
#endif
    }

    /** 通过真实子进程验证项目切换确实改变 cwd，并保留历史会话启动参数。 */
    void workspaceSwitchChangesRealProcessDirectory()
    {
        QTemporaryDir config;
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(config.isValid() && first.isValid() && second.isValid());
        const auto format = QSettings::defaultFormat();
        const auto organization = QCoreApplication::organizationName();
        const auto application = QCoreApplication::applicationName();
        const auto restore = qScopeGuard([=] {
            QSettings::setDefaultFormat(format);
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
        });
        QCoreApplication::setOrganizationName("WorkspaceSwitchTest");
        QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        {
            AppSettings settings;
            settings.setPiExecutable(QCoreApplication::applicationFilePath());
            settings.setPiProfilePath(config.path());
            settings.setWorkspacePath(first.path());
        }
        AppController app;
        QTRY_VERIFY(app.agent()->connected());
        QCOMPARE(app.agent()->sessionName(), first.path());
        QVERIFY(app.openWorkspace(second.path()));
        QVERIFY(app.workspaceSwitching());
        QVERIFY(!app.agent()->connected());
        QVERIFY(!app.agent()->prompt("切换期间禁止发送"));
        QVERIFY(!app.openWorkspace(first.path()));
        QTRY_VERIFY(!app.workspaceSwitching());
        QTRY_VERIFY(app.agent()->connected());
        QCOMPARE(app.agent()->sessionName(), second.path());
        QCOMPARE(app.settings()->workspacePath(), second.path());
        QVERIFY(!app.openWorkspace(second.filePath("missing")));
        QCOMPARE(app.settings()->workspacePath(), second.path());
        QFile history(config.filePath("history with spaces.jsonl"));
        QVERIFY(history.open(QIODevice::WriteOnly));
        history.write(QJsonDocument(QJsonObject{{"type", "session"}, {"cwd", first.path()}}).toJson(QJsonDocument::Compact));
        history.write("\n");
        history.close();
        QVERIFY(!app.openWorkspace(second.path(), history.fileName()));
        QVERIFY(app.openWorkspace(first.path(), history.fileName()));
        QTRY_VERIFY(!app.workspaceSwitching());
        QTRY_VERIFY(app.agent()->connected());
        QCOMPARE(app.agent()->sessionName(), first.path());
        QCOMPARE(app.agent()->sessionFile(), history.fileName());
        QVERIFY(app.openWorkspace(second.path()));
        QTRY_VERIFY(!app.workspaceSwitching());
        QCOMPARE(app.agent()->sessionFile(), QString());
        QCOMPARE(app.agent()->sessionName(), second.path());
        QVERIFY(app.agent()->prompt("验证忙碌锁"));
        QVERIFY(app.agent()->busy());
        QVERIFY(!app.openWorkspace(first.path()));
        QCOMPARE(app.settings()->workspacePath(), second.path());
    }

    /** 添加工作文件夹后自动切换真实子进程 cwd；重复、缺失和忙碌时保持原配置。 */
    void addingProjectFolderSwitchesWorkspace()
    {
        QTemporaryDir config;
        QTemporaryDir first;
        QTemporaryDir second;
        QTemporaryDir third;
        QVERIFY(config.isValid() && first.isValid() && second.isValid() && third.isValid());
        const auto format = QSettings::defaultFormat();
        const auto organization = QCoreApplication::organizationName();
        const auto application = QCoreApplication::applicationName();
        const auto restore = qScopeGuard([=] {
            QSettings::setDefaultFormat(format);
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
        });
        QCoreApplication::setOrganizationName("AddProjectFolderTest");
        QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        {
            AppSettings settings;
            settings.setPiExecutable(QCoreApplication::applicationFilePath());
            settings.setPiProfilePath(config.path());
            settings.setWorkspacePath(first.path());
        }
        AppController app;
        QTRY_VERIFY(app.agent()->connected());
        QVERIFY(app.projects()->saveProject("", "添加即切换"));
        const QString project = app.projects()->rows()[0].toMap().value("id").toString();
        QVERIFY(!app.addProjectFolder("missing-project", second.path()));
        QCOMPARE(app.settings()->workspacePath(), first.path());
        QVERIFY(!app.addProjectFolder(project, second.filePath("missing")));
        QCOMPARE(app.projects()->rows().size(), 1);
        QVERIFY(app.addProjectFolder(project, QUrl::fromLocalFile(second.path()).toString()));
        QCOMPARE(app.settings()->workspacePath(), second.path());
        QTRY_VERIFY(!app.workspaceSwitching());
        QTRY_VERIFY(app.agent()->connected());
        QCOMPARE(app.agent()->sessionName(), second.path());
        QCOMPARE(app.projects()->rows().size(), 2);
        QVERIFY(!app.addProjectFolder(project, second.path()));
        QCOMPARE(app.projects()->rows().size(), 2);
        QVERIFY(app.agent()->prompt("验证添加期间的忙碌保护"));
        QVERIFY(!app.addProjectFolder(project, third.path()));
        QCOMPARE(app.settings()->workspacePath(), second.path());
        QCOMPARE(app.projects()->rows().size(), 2);
    }

    /** 无效工作目录必须拒绝启动，不能静默回退到应用所在目录。 */
    void missingWorkspaceDoesNotFallback()
    {
        QTemporaryDir profile;
        PiProcess process;
        process.setExecutable(QCoreApplication::applicationFilePath());
        process.setConfigDirectory(profile.path());
        process.setWorkingDirectory(profile.filePath("missing"));
        QSignalSpy errors(&process, &PiProcess::processError);
        QVERIFY(!process.start());
        QCOMPARE(errors.count(), 1);
        QVERIFY(!process.active());
    }

    /** 队列事件只更新摘要；真正投递的用户事件才进入历史，中止取回不覆盖草稿。 */
    void queueEventsAndRestore()
    {
        PiProcess process;
        PiRpcClient rpc(&process);
        ChatModel model;
        AgentSessionController agent(&process, &rpc, &model);
        QSignalSpy restored(&agent, &AgentSessionController::restoreDraftRequested);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "agent_start"}}));
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "queue_update"},
            {"steering", QJsonArray{"guide"}}, {"followUp", QJsonArray{"later"}}}));
        QVERIFY(agent.queueText().contains("guide"));
        QVERIFY(agent.queueText().contains("later"));
        QCOMPARE(model.rowCount(), 0);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "message_start"},
            {"message", QJsonObject{{"role", "user"}, {"content", "guide"}}}}));
        QCOMPARE(model.rowCount(), 1);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "response"}, {"command", "clear_queue"},
            {"success", true}, {"data", QJsonObject{{"steering", QJsonArray{}}, {"followUp", QJsonArray{"later"}}}}}));
        QCOMPARE(restored.count(), 1);
        QCOMPARE(restored.first().first().toString(), QString("later"));
        QVERIFY(agent.busy());
    }

    /** 空内容错误不能被当作纯工具消息移除，历史错误也必须保留详情。 */
    void errorMessagesRemainVisible()
    {
        ChatModel model;
        model.finishAssistant({}, true);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.data(model.index(0), ChatModel::ErrorRole).toBool());
        model.replaceFromMessages(QJsonArray{QJsonObject{{"role", "assistant"}, {"content", QJsonArray{}},
            {"stopReason", "error"}, {"errorMessage", "429 quota exceeded"}}});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ChatModel::ContentRole).toString(), QString("429 quota exceeded"));
        QCOMPARE(model.data(model.index(0), ChatModel::StateRole).toString(), QString("failed"));
    }

    /** 验证启动统计、错误详情、重试等待及失败统计不会错误解除忙碌状态。 */
    void agentEventsAndStats()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        PiProcess process;
        PiRpcClient rpc(&process);
        ChatModel model;
        AgentSessionController agent(&process, &rpc, &model);
        process.setExecutable(QCoreApplication::applicationFilePath());
        process.setConfigDirectory(profile.path());
        process.setWorkingDirectory(profile.path());
        QVERIFY(process.start());
        QTRY_COMPARE(agent.sessionStats().value("tokens").toMap().value("total").toInt(), 105000);
        QCOMPARE(agent.sessionStats().value("contextUsage").toMap().value("tokens").toInt(), 60000);
        QVERIFY(agent.prompt("test"));
        QVERIFY(agent.busy());
        QVERIFY(agent.prompt("steer while busy"));
        QTRY_VERIFY(agent.queueText().contains(QStringLiteral("引导：steer while busy")));
        QVERIFY(agent.prompt("follow up while busy", true));
        QTRY_VERIFY(agent.queueText().contains(QStringLiteral("后续：follow up while busy")));
        QVERIFY(!agent.workingText().isEmpty());
        const QString word = agent.workingText();
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "agent_start"}}));
        QCOMPARE(agent.workingText(), word);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "message_end"}, {"message", QJsonObject{
            {"role", "assistant"}, {"content", QJsonArray{}}, {"stopReason", "error"}, {"errorMessage", "API quota error"}}}}));
        QVERIFY(model.data(model.index(model.rowCount() - 1), ChatModel::ContentRole).toString().contains("API quota error"));
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "auto_retry_start"}, {"attempt", 1}, {"maxAttempts", 3},
                                             {"delayMs", 2000}, {"errorMessage", "overloaded"}}));
        QVERIFY(agent.statusText().contains("overloaded"));
        QVERIFY(agent.busy());
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "response"}, {"command", "get_session_stats"},
                                             {"id", "stale"}, {"success", false}, {"error", "unsupported"}}));
        QVERIFY(agent.busy());
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "agent_settled"}}));
        QVERIFY(!agent.busy());
        QVERIFY(agent.statusText().contains(QStringLiteral("错误")));
        process.stop();
        QTRY_VERIFY(!process.active());
    }

    /** 十秒换词不连续重复、不覆盖工具状态，退出忙碌状态后必须停止。 */
    void workingWordsRotateAndStop()
    {
        PiProcess process;
        PiRpcClient rpc(&process);
        ChatModel model;
        AgentSessionController agent(&process, &rpc, &model);
        auto *timer = agent.findChild<QTimer *>("workingWordTimer");
        QVERIFY(timer);
        QCOMPARE(timer->interval(), 10000);
        QVERIFY(!timer->isActive());
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "agent_start"}}));
        QVERIFY(timer->isActive());
        const QString first = agent.workingText();
        // 确认生产间隔后缩短测试计时，验证相同的定时器及状态分支。
        timer->setInterval(100);
        QTRY_VERIFY(agent.workingText() != first);
        QCOMPARE(agent.statusText(), agent.workingText() + QStringLiteral("…"));
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "tool_execution_start"},
            {"toolCallId", "tool-1"}, {"toolName", "bash"}, {"args", QJsonObject{}}}));
        const QString toolStatus = agent.statusText();
        QSignalSpy changed(&agent, &AgentSessionController::workingTextChanged);
        QTRY_VERIFY(changed.count() > 0);
        QCOMPARE(agent.statusText(), toolStatus);
        process.processError(QStringLiteral("测试连接失败"));
        QVERIFY(!timer->isActive());
        const int stoppedCount = changed.count();
        QTest::qWait(250);
        QCOMPARE(changed.count(), stoppedCount);
    }

    /** 新会话取消时不触发折叠信号，只有确认成功后才触发。 */
    void newSessionConfirmation()
    {
        PiProcess process;
        PiRpcClient rpc(&process);
        ChatModel model;
        AgentSessionController agent(&process, &rpc, &model);
        QSignalSpy spy(&agent, &AgentSessionController::newSessionCreated);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "response"}, {"command", "new_session"}, {"success", true},
                                             {"data", QJsonObject{{"cancelled", true}}}}));
        QCOMPARE(spy.count(), 0);
        rpc.eventReceived(PiEvent(QJsonObject{{"type", "response"}, {"command", "new_session"}, {"success", true},
                                             {"data", QJsonObject{{"cancelled", false}}}}));
        QCOMPARE(spy.count(), 1);
    }

    /** 连续 stderr 仅维护一张诊断卡片，不将普通诊断伪装成失败。 */
    void diagnosticsAreBounded()
    {
        PiProcess process;
        PiRpcClient rpc(&process);
        ChatModel model;
        AgentSessionController agent(&process, &rpc, &model);
        process.errorReceived(QByteArray(20000, 'x'));
        QTRY_COMPARE(model.rowCount(), 1);
        process.errorReceived("\nlatest diagnostic");
        QTRY_VERIFY(model.data(model.index(0), ChatModel::ContentRole).toString().contains("latest diagnostic"));
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.data(model.index(0), ChatModel::ContentRole).toString().size() < 17000);
        QVERIFY(!model.data(model.index(0), ChatModel::ErrorRole).toBool());
    }

    /** 大批 JSONL 分片到达时保持顺序，同时让事件循环有机会处理其它任务。 */
    void rpcDispatchIsIncremental()
    {
        PiProcess process;
        PiRpcClient rpc(&process);
        QSignalSpy spy(&rpc, &PiRpcClient::eventReceived);
        QByteArray batch;
        for (int i = 0; i < 500; ++i)
            batch += "{\"type\":\"test\",\"sequence\":" + QByteArray::number(i) + "}\n";
        process.outputReceived(batch.left(batch.size() - 2));
        QCOMPARE(spy.count(), 0);
        process.outputReceived(batch.right(2));
        QTRY_COMPARE(spy.count(), 500);
        for (int i = 0; i < spy.count(); ++i)
            QCOMPARE(qvariant_cast<PiEvent>(spy.at(i).at(0)).payload().value("sequence").toInt(), i);
    }
};

/** 正常执行单元测试；由 PiProcess 启动时充当标准输入输出 RPC 桩。 */
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--mode"))
        return runFakePi();
    AgentSessionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "AgentSessionTest.moc"
