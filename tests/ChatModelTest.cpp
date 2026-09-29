#include "chat/ChatModel.h"
#include "config/AppSettings.h"
#include "session/SessionModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QtTest>

/**
 * 验证聊天模型的流式更新、工具状态和历史消息恢复行为。
 */
class ChatModelTest final : public QObject
{
    Q_OBJECT

private slots:
    /** 删除候选会持久化隐藏，重扫和重启不复现，磁盘和正在使用的配置均保留。 */
    void removedDiscoveryEntriesStayHidden()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto originalFormat = QSettings::defaultFormat();
        const auto restore = qScopeGuard([=] { QSettings::setDefaultFormat(originalFormat); });
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        const QString profile = temporary.filePath("profiles/test");
        QVERIFY(QDir().mkpath(profile));
        const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        {
            AppSettings settings;
            settings.setPiProfilePath(profile);
            settings.setPiExecutable(executable);
            settings.refreshDiscovery();
            QVERIFY(settings.profileDirectories().contains(profile));
            QVERIFY(settings.executablePaths().contains(executable));
            QVERIFY(!settings.removeDiscoveryEntry("invalid", profile));
            QVERIFY(!settings.discoveryError().isEmpty());
            QVERIFY(!settings.removeDiscoveryEntry("profile", temporary.filePath("missing")));
            QVERIFY(settings.removeDiscoveryEntry("profile", profile));
            QVERIFY(settings.discoveryError().isEmpty());
            QVERIFY(settings.removeDiscoveryEntry("executable", executable));
            QVERIFY(!settings.removeDiscoveryEntry("profile", profile));
            settings.refreshDiscovery();
            QVERIFY(!settings.profileDirectories().contains(profile));
            QVERIFY(!settings.executablePaths().contains(executable));
            QCOMPARE(settings.piProfilePath(), profile);
            QCOMPARE(settings.piExecutable(), executable);
        }
        AppSettings reloaded;
        QVERIFY(!reloaded.profileDirectories().contains(profile));
        QVERIFY(!reloaded.executablePaths().contains(executable));
        QCOMPARE(reloaded.piProfilePath(), profile);
        QCOMPARE(reloaded.piExecutable(), executable);
        QVERIFY(QFileInfo(profile).isDir());
        QVERIFY(QFileInfo(executable).isFile());
    }

    /** 标准配置与自定义根目录均可发现，仅扫描 profiles 的直接子目录。 */
    void discoversProfileDirectories()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QDir root(home.path());
        QVERIFY(root.mkpath(".pi/agent"));
        QVERIFY(root.mkpath(".pi/profiles/openai/sessions/nested"));
        QVERIFY(root.mkpath(".pi/profiles/Desktop"));
        QVERIFY(root.mkpath("custom/profiles/team"));
        QVERIFY(root.mkpath("custom/profiles/other"));
        QVERIFY(root.mkpath("saved"));
        QFile file(home.filePath(".pi/profiles/not-a-directory"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        int skipped = -1;
        const auto paths = AppSettings::discoverProfiles(home.path(), home.filePath("custom/profiles/team"),
                                                         home.filePath("saved"), &skipped);
        QCOMPARE(paths.size(), 6);
        QCOMPARE(paths.first(), QFileInfo(home.filePath("saved")).canonicalFilePath());
        QVERIFY(paths.contains(QFileInfo(home.filePath(".pi/agent")).canonicalFilePath()));
        QVERIFY(paths.contains(QFileInfo(home.filePath("custom/profiles/other")).canonicalFilePath()));
        QVERIFY(!paths.contains(QFileInfo(home.filePath(".pi/profiles/openai/sessions")).canonicalFilePath()));
        QCOMPARE(skipped, 0);
    }

    /** 无效配置被跳过，重复路径只保留一项，空 Profile 无需凭据文件也能列出。 */
    void profileDiscoveryDeduplicatesAndRejectsInvalidPaths()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QVERIFY(QDir(home.path()).mkpath(".pi/profiles/empty"));
        const QString profile = home.filePath(".pi/profiles/empty");
        const auto paths = AppSettings::discoverProfiles(home.path(), profile + "/.", profile);
        QCOMPARE(paths, QStringList{QFileInfo(profile).canonicalFilePath()});
        int skipped = 0;
        const auto invalid = AppSettings::discoverProfiles(home.path(), "relative/path", home.filePath("missing"), &skipped);
        QCOMPARE(invalid, paths);
        QCOMPARE(skipped, 3);
        QVERIFY(AppSettings::discoverProfiles({}, {}, {}).isEmpty());
    }

    /** 刷新可增删同级候选，但不能覆盖用户当前 Profile 和 Pi 命令。 */
    void profileDiscoveryRefreshPreservesConfiguration()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString profile = temporary.filePath("profiles/custom");
        const QString sibling = temporary.filePath("profiles/new-profile");
        QVERIFY(QDir().mkpath(profile));
        QVERIFY(QDir().mkpath(sibling));
        const auto originalFormat = QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        bool discovered = false;
        bool removed = false;
        bool unchanged = false;
        int notifications = 0;
        {
            AppSettings settings;
            settings.setPiProfilePath(profile);
            settings.setPiExecutable("custom-pi-command");
            QSignalSpy changed(&settings, &AppSettings::discoveryChanged);
            settings.refreshDiscovery();
            discovered = settings.profileDirectories().contains(QFileInfo(sibling).canonicalFilePath());
            QDir().rmdir(sibling);
            settings.refreshDiscovery();
            removed = !settings.profileDirectories().contains(sibling);
            unchanged = settings.piProfilePath() == profile && settings.piExecutable() == "custom-pi-command";
            notifications = changed.count();
        }
        QSettings::setDefaultFormat(originalFormat);
        QVERIFY(discovered);
        QVERIFY(removed);
        QVERIFY(unchanged);
        QCOMPARE(notifications, 2);
    }

    /** 使用临时 INI 存储验证 Profile 配置持久化，不接触用户注册表。 */
    void profilePathPersists()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto originalFormat = QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QString saved;
        int changes = 0;
        {
            AppSettings settings;
            QSignalSpy changed(&settings, &AppSettings::piProfilePathChanged);
            settings.setPiProfilePath(dir.path());
            settings.setPiProfilePath(dir.path());
            settings.setPiProfilePath("");
            changes = changed.count();
        }
        {
            AppSettings reloaded;
            saved = reloaded.piProfilePath();
        }
        QSettings::setDefaultFormat(originalFormat);
        QCOMPARE(saved, dir.path());
        QCOMPARE(changes, 1);
    }

    /** 切换 Profile 时，即使旧扫描尚未完成也不能回填旧目录的数据。 */
    void profileSwitchDiscardsOldScan()
    {
        QTemporaryDir oldProfile;
        QTemporaryDir newProfile;
        QFile file(oldProfile.filePath("old.jsonl"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"type\":\"session\",\"id\":\"old\"}\n");
        file.close();
        SessionModel model;
        model.setSessionRoot(oldProfile.path());
        model.refresh();
        model.setSessionRoot(newProfile.path());
        model.refresh();
        QSignalSpy completed(&model, &SessionModel::refreshCompleted);
        QVERIFY(completed.wait());
        QCOMPARE(model.rowCount(), 0);
    }

    /**
     * 验证多个文本增量只更新同一个助手条目。
     */
    void streamingUpdatesSingleAssistantRow();

    /**
     * 验证工具卡片从运行到完成的状态转换。
     */
    void toolLifecycleUpdatesExistingRow();

    /**
     * 验证 Pi 历史消息能恢复为消息和工具卡片。
     */
    void historyMessagesRestoreToolCard();

    /**
     * 验证 Session 文件会在后台线程完成扫描并返回元数据。
     */
    void sessionScanRunsAsynchronously();

    /** 跨存储目录的会话按更新时间排序，缓存刷新后顺序保持一致。 */
    void sessionsSortAcrossStorageFolders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("a"));
        QVERIFY(QDir(dir.path()).mkpath("b"));
        int age = 0;
        for (const QString &name : {QString("b/1"), QString("a/1"), QString("a/2")}) {
            QFile file(dir.filePath(name + ".jsonl"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{\"type\":\"session\",\"id\":\"test\",\"cwd\":\"same-project\"}\n");
            file.flush();
            QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-age * 60), QFileDevice::FileModificationTime));
            ++age;
        }
        SessionModel model;
        model.setSessionRoot(dir.path());
        QSignalSpy completed(&model, &SessionModel::refreshCompleted);
        model.refresh();
        QVERIFY(completed.wait());
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.data(model.index(0), SessionModel::PathRole).toString(), dir.path() + "/b/1.jsonl");
        QCOMPARE(model.data(model.index(1), SessionModel::PathRole).toString(), dir.path() + "/a/1.jsonl");
        QCOMPARE(model.data(model.index(2), SessionModel::PathRole).toString(), dir.path() + "/a/2.jsonl");
        model.refresh();
        QVERIFY(completed.wait());
        QCOMPARE(model.data(model.index(0), SessionModel::PathRole).toString(), dir.path() + "/b/1.jsonl");
    }

    /** 全局 Token 按时间范围聚合，追加扫描不能重复累计旧 usage。 */
    void sessionTokenUsageAggregatesIncrementally()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile first(dir.filePath(QStringLiteral("first.jsonl")));
        QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Text));
        first.write("{\"type\":\"session\",\"id\":\"session-1\",\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"cwd\":\"E:/Project\"}\n");
        first.write("{\"type\":\"message\",\"id\":\"assistant-1\",\"timestamp\":\"2026-01-05T01:00:00.000Z\",\"message\":{\"role\":\"assistant\",\"usage\":{\"input\":100,\"output\":20,\"cacheRead\":30,\"cacheWrite\":0,\"cost\":{\"total\":0.10}}}}\n");
        first.write("{\"type\":\"message\",\"id\":\"tool-1\",\"timestamp\":\"2026-01-06T02:00:00.000Z\",\"message\":{\"role\":\"toolResult\",\"usage\":{\"input\":5,\"output\":0,\"cacheRead\":0,\"cacheWrite\":0,\"cost\":{\"total\":0.02}}}}\n");
        first.write("{\"type\":\"compaction\",\"id\":\"compact-1\",\"timestamp\":\"2026-01-06T03:00:00.000Z\",\"usage\":{\"input\":50,\"output\":10,\"cacheRead\":20,\"cacheWrite\":0,\"cost\":{\"total\":0.08}}}\n");
        first.close();

        QFile second(dir.filePath(QStringLiteral("second.jsonl")));
        QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Text));
        second.write("{\"type\":\"session\",\"id\":\"session-2\",\"timestamp\":\"2026-01-20T00:00:00.000Z\",\"cwd\":\"E:/Project\"}\n");
        second.write("{\"type\":\"message\",\"id\":\"assistant-2\",\"timestamp\":\"2026-01-20T04:00:00.000Z\",\"message\":{\"role\":\"assistant\",\"usage\":{\"input\":700,\"output\":200,\"cacheRead\":100,\"cacheWrite\":0,\"cost\":{\"total\":0.50}}}}\n");
        second.close();

        SessionModel model;
        model.setSessionRoot(dir.path());
        QSignalSpy completed(&model, &SessionModel::refreshCompleted);
        model.refresh();
        QVERIFY(completed.wait());
        QVERIFY(model.setCustomTokenUsageRange(QStringLiteral("2026-01-01"), QStringLiteral("2026-01-07")));

        QVariantMap view = model.tokenUsageView();
        QVariantMap tokens = view.value(QStringLiteral("tokens")).toMap();
        QCOMPARE(tokens.value(QStringLiteral("input")).toLongLong(), 155);
        QCOMPARE(tokens.value(QStringLiteral("output")).toLongLong(), 30);
        QCOMPARE(tokens.value(QStringLiteral("cacheRead")).toLongLong(), 50);
        QCOMPARE(tokens.value(QStringLiteral("total")).toLongLong(), 235);
        QCOMPARE(view.value(QStringLiteral("sessionCount")).toInt(), 1);
        QCOMPARE(view.value(QStringLiteral("allSessionCount")).toInt(), 2);
        QCOMPARE(view.value(QStringLiteral("eventCount")).toInt(), 3);
        QCOMPARE(view.value(QStringLiteral("points")).toList().size(), 7);
        QVERIFY(qFuzzyCompare(view.value(QStringLiteral("cost")).toDouble() + 1.0, 1.20));

        model.setTokenUsagePeriod(QStringLiteral("total"));
        view = model.tokenUsageView();
        tokens = view.value(QStringLiteral("tokens")).toMap();
        QCOMPARE(tokens.value(QStringLiteral("total")).toLongLong(), 1235);
        QCOMPARE(view.value(QStringLiteral("sessionCount")).toInt(), 2);
        QCOMPARE(view.value(QStringLiteral("eventCount")).toInt(), 4);

        QVERIFY(first.open(QIODevice::Append | QIODevice::Text));
        first.write("{\"type\":\"message\",\"id\":\"assistant-3\",\"timestamp\":\"2026-01-06T05:00:00.000Z\",\"message\":{\"role\":\"assistant\",\"usage\":{\"input\":10,\"output\":0,\"cacheRead\":0,\"cacheWrite\":0,\"cost\":{\"total\":0.01}}}}\n");
        first.close();
        model.refresh();
        QVERIFY(completed.wait());
        QVERIFY(model.setCustomTokenUsageRange(QStringLiteral("2026-01-01"), QStringLiteral("2026-01-07")));
        view = model.tokenUsageView();
        tokens = view.value(QStringLiteral("tokens")).toMap();
        QCOMPARE(tokens.value(QStringLiteral("total")).toLongLong(), 245);
        QCOMPARE(view.value(QStringLiteral("eventCount")).toInt(), 4);
        QVERIFY(!model.setCustomTokenUsageRange(QStringLiteral("2026-01-08"), QStringLiteral("2026-01-01")));
        QVERIFY(!model.tokenUsageView().value(QStringLiteral("error")).toString().isEmpty());
    }

    /** 不同 Profile 的 Token 统计可切换，活动 Session 列表保持不变。 */
    void tokenUsageSwitchesProfileWithoutChangingSessions();
};

/**
 * 流式增量应拼接内容，message_end 应完成同一行。
 */
void ChatModelTest::streamingUpdatesSingleAssistantRow()
{
    ChatModel model;
    model.ensureStreamingAssistant();
    model.appendAssistantDelta(QStringLiteral("Hello"));
    model.appendAssistantDelta(QStringLiteral(" world"));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ChatModel::ContentRole).toString(), QStringLiteral("Hello world"));
    QCOMPARE(model.data(model.index(0), ChatModel::StateRole).toString(), QStringLiteral("streaming"));

    model.finishAssistant(QStringLiteral("Hello world"), false);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ChatModel::StateRole).toString(), QStringLiteral("completed"));
}

/**
 * 相同 toolCallId 的更新必须复用原卡片。
 */
void ChatModelTest::toolLifecycleUpdatesExistingRow()
{
    ChatModel model;
    model.startTool(QStringLiteral("call-1"), QStringLiteral("read"), QStringLiteral("{\"path\":\"a.cpp\"}"));
    model.updateTool(QStringLiteral("call-1"), QStringLiteral("partial"));
    model.finishTool(QStringLiteral("call-1"), QStringLiteral("complete"), false);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ChatModel::ToolOutputRole).toString(), QStringLiteral("complete"));
    QCOMPARE(model.data(model.index(0), ChatModel::StateRole).toString(), QStringLiteral("completed"));
    QCOMPARE(model.data(model.index(0), ChatModel::ErrorRole).toBool(), false);
}

/**
 * assistant toolCall 与 toolResult 应按 ID 合并为一张完成卡片。
 */
void ChatModelTest::historyMessagesRestoreToolCard()
{
    const QJsonArray messages {
        QJsonObject {
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), QStringLiteral("检查文件")},
            {QStringLiteral("timestamp"), 1000}
        },
        QJsonObject {
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QJsonArray {
                QJsonObject {{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), QStringLiteral("我来读取。")}},
                QJsonObject {{QStringLiteral("type"), QStringLiteral("toolCall")},
                             {QStringLiteral("id"), QStringLiteral("call-1")},
                             {QStringLiteral("name"), QStringLiteral("read")},
                             {QStringLiteral("arguments"), QJsonObject {{QStringLiteral("path"), QStringLiteral("a.cpp")}}}}
            }},
            {QStringLiteral("timestamp"), 2000}
        },
        QJsonObject {
            {QStringLiteral("role"), QStringLiteral("toolResult")},
            {QStringLiteral("toolCallId"), QStringLiteral("call-1")},
            {QStringLiteral("toolName"), QStringLiteral("read")},
            {QStringLiteral("content"), QJsonArray {
                QJsonObject {{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), QStringLiteral("file data")}}
            }},
            {QStringLiteral("isError"), false},
            {QStringLiteral("timestamp"), 3000}
        }
    };

    ChatModel model;
    model.replaceFromMessages(messages);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(2), ChatModel::EntryTypeRole).toString(), QStringLiteral("tool"));
    QCOMPARE(model.data(model.index(2), ChatModel::ToolOutputRole).toString(), QStringLiteral("file data"));
    QCOMPARE(model.data(model.index(2), ChatModel::StateRole).toString(), QStringLiteral("completed"));
}

/**
 * 不同 Profile 的 Token 统计可切换，活动 Session 列表保持不变。
 */
void ChatModelTest::tokenUsageSwitchesProfileWithoutChangingSessions()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString profileA = root.filePath(QStringLiteral("profiles/A"));
    const QString profileB = root.filePath(QStringLiteral("profiles/B"));
    QVERIFY(QDir().mkpath(QDir(profileA).filePath(QStringLiteral("sessions"))));
    QVERIFY(QDir().mkpath(QDir(profileB).filePath(QStringLiteral("sessions"))));

    QFile first(QDir(profileA).filePath(QStringLiteral("sessions/a.jsonl")));
    QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Text));
    first.write("{\"type\":\"session\",\"id\":\"a\",\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"cwd\":\"E:/A\"}\n");
    first.write("{\"type\":\"message\",\"timestamp\":\"2026-01-05T01:00:00.000Z\",\"message\":{\"role\":\"assistant\",\"usage\":{\"input\":10,\"output\":2}}}\n");
    first.close();

    QFile second(QDir(profileB).filePath(QStringLiteral("sessions/b.jsonl")));
    QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Text));
    second.write("{\"type\":\"session\",\"id\":\"b\",\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"cwd\":\"E:/B\"}\n");
    second.write("{\"type\":\"message\",\"timestamp\":\"2026-01-05T01:00:00.000Z\",\"message\":{\"role\":\"assistant\",\"usage\":{\"input\":90,\"output\":8}}}\n");
    second.close();

    SessionModel model;
    model.setSessionRoot(QDir(profileA).filePath(QStringLiteral("sessions")));
    model.setTokenUsageProfiles({profileA, profileB});
    QVERIFY(model.setTokenUsageProfile(profileA));
    QSignalSpy completed(&model, &SessionModel::refreshCompleted);
    model.refresh();
    QVERIFY(completed.wait(3000));
    QVERIFY(model.setCustomTokenUsageRange(QStringLiteral("2026-01-01"), QStringLiteral("2026-01-07")));
    QCOMPARE(model.tokenUsageView().value(QStringLiteral("tokens")).toMap()
                 .value(QStringLiteral("total")).toLongLong(), 12);
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.setTokenUsageProfile(profileB));
    QTRY_COMPARE_WITH_TIMEOUT(model.tokenUsageView().value(QStringLiteral("tokens")).toMap()
                                  .value(QStringLiteral("total")).toLongLong(), 98, 3000);
    QCOMPARE(model.tokenUsageProfile(), QDir::cleanPath(profileB));
    QCOMPARE(model.rowCount(), 1);
}

/**
 * 后台扫描完成后应恢复名称、项目路径和首条用户消息。
 */
void ChatModelTest::sessionScanRunsAsynchronously()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QFile sessionFile(temporaryDirectory.filePath(QStringLiteral("sample.jsonl")));
    QVERIFY(sessionFile.open(QIODevice::WriteOnly | QIODevice::Text));
    sessionFile.write("{\"type\":\"session\",\"version\":3,\"id\":\"session-1\",\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"cwd\":\"E:/Project\"}\n");
    sessionFile.write("{\"type\":\"message\",\"id\":\"entry-1\",\"parentId\":null,\"message\":{\"role\":\"user\",\"content\":\"测试会话\"}}\n");
    sessionFile.write("{\"type\":\"session_info\",\"id\":\"entry-2\",\"parentId\":\"entry-1\",\"name\":\"后台扫描测试\"}\n");
    sessionFile.close();

    SessionModel model;
    model.setSessionRoot(temporaryDirectory.path());
    QSignalSpy completedSpy(&model, &SessionModel::refreshCompleted);
    model.refresh();

    QVERIFY(completedSpy.wait(3000));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), SessionModel::NameRole).toString(), QStringLiteral("后台扫描测试"));
    QCOMPARE(model.data(model.index(0), SessionModel::ProjectPathRole).toString(), QStringLiteral("E:/Project"));
}

QTEST_GUILESS_MAIN(ChatModelTest)
#include "ChatModelTest.moc"
