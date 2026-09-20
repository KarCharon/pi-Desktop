#include "session/ProjectModel.h"
#include "session/SessionModel.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

/** 使用隔离的应用设置验证项目持久化及真实目录分组。 */
class ProjectModelTest final : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_settings;
    /** 创建测试会话，存储目录故意与真实 cwd 无关。 */
    void writeSession(const QString &filePath, const QString &cwd)
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"type", "session"}, {"id", filePath}, {"cwd", cwd}}).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
private slots:
    /** 所有项目配置仅写入临时 INI 目录。 */
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("ProjectModelTests");
        QCoreApplication::setApplicationName("Isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    /** 每个用例从空项目配置开始。 */
    void init() { QSettings().clear(); }

    /** 损坏配置应明确报错，读取过程不能覆盖原始配置。 */
    void invalidConfigurationRemainsUntouched()
    {
        const QByteArray invalid("[{\"name\":\"missing id\"}]");
        QSettings().setValue("navigation/projects", invalid);
        SessionModel sessions;
        ProjectModel projects(&sessions);
        QVERIFY(!projects.error().isEmpty());
        QVERIFY(projects.rows().isEmpty());
        QCOMPARE(QSettings().value("navigation/projects").toByteArray(), invalid);
    }

    /** 空项目可创建，项目和文件夹改名不改变稳定编号与真实路径。 */
    void projectPersistenceAndAliases()
    {
        QTemporaryDir workspace;
        SessionModel sessions;
        ProjectModel projects(&sessions);
        QVERIFY(projects.rows().isEmpty());
        QVERIFY(!projects.saveProject("", "  "));
        QVERIFY(projects.saveProject("", "桌面工具"));
        const QString pid = projects.rows()[0].toMap().value("id").toString();
        QVERIFY(projects.addFolder(pid, QUrl::fromLocalFile(workspace.path()).toString(), "客户端"));
        QCOMPARE(projects.rows().size(), 2);
        const QString fid = projects.rows()[1].toMap().value("id").toString();
        QVERIFY(!projects.addFolder(pid, workspace.path(), "重复"));
        QVERIFY(!projects.addFolder(pid, workspace.filePath("missing")));
        QVERIFY(projects.saveProject(pid, "新名称"));
        QVERIFY(projects.renameFolder(fid, "新别名"));
        ProjectModel restored(&sessions);
        QCOMPARE(restored.rows()[0].toMap().value("title").toString(), QString("新名称"));
        QCOMPARE(restored.rows()[1].toMap().value("title").toString(), QString("新别名"));
        QCOMPARE(restored.rows()[1].toMap().value("id").toString(), fid);
        QCOMPARE(restored.rows()[1].toMap().value("path").toString(), workspace.path());
        QVERIFY(restored.removeEntry(pid));
        QVERIFY(restored.rows().isEmpty());
        QVERIFY(QDir(workspace.path()).exists());
    }

    /** 按 cwd 匹配跨存储目录的历史，移除分组不删除会话或工作文件夹。 */
    void groupingUsesWorkspaceNotProfileFolder()
    {
        QTemporaryDir profile;
        QTemporaryDir workspace;
        QTemporaryDir other;
        QVERIFY(QDir(profile.path()).mkdir("encoded-profile-folder"));
        writeSession(profile.filePath("one.jsonl"), workspace.path());
        writeSession(profile.filePath("encoded-profile-folder/two.jsonl"), workspace.path());
        writeSession(profile.filePath("three.jsonl"), other.path());
        SessionModel sessions;
        ProjectModel projects(&sessions);
        QVERIFY(projects.saveProject("", "项目"));
        const QString pid = projects.rows()[0].toMap().value("id").toString();
        QVERIFY(projects.addFolder(pid, workspace.path(), "工作区"));
        sessions.setSessionRoot(profile.path());
        sessions.refresh();
        QTRY_COMPARE(sessions.count(), 3);
        QCOMPARE(projects.rows().size(), 7);
        QCOMPARE(projects.rows()[2].toMap().value("kind").toString(), QString("session"));
        QCOMPARE(projects.rows()[3].toMap().value("kind").toString(), QString("session"));
        QCOMPARE(projects.rows()[2].toMap().value("projectId").toString(), pid);
        QCOMPARE(projects.rows()[3].toMap().value("projectId").toString(), pid);
        QCOMPARE(projects.rows()[4].toMap().value("title").toString(), QString("未分组"));
        QVERIFY(projects.removeEntry(projects.rows()[1].toMap().value("id").toString()));
        QVERIFY(QFile::exists(profile.filePath("one.jsonl")));
        QVERIFY(QFile::exists(profile.filePath("encoded-profile-folder/two.jsonl")));
        QCOMPARE(sessions.count(), 3);
        QCOMPARE(projects.rows().size(), 7);
        QCOMPARE(projects.rows()[1].toMap().value("title").toString(), QString("未分组"));
        QTemporaryDir newProfile;
        sessions.setSessionRoot(newProfile.path());
        sessions.refresh();
        QTRY_VERIFY(!sessions.loading());
        QCOMPARE(projects.rows().size(), 1);
        QCOMPARE(projects.rows()[0].toMap().value("id").toString(), pid);
    }
};

QTEST_GUILESS_MAIN(ProjectModelTest)
#include "ProjectModelTest.moc"
