#include "app/AppController.h"
#include "config/AppSettings.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLoggingCategory>
#include <QQuickStyle>

/** 安装或移除当前用户的资源管理器菜单，不修改系统级注册表。 */
static bool configureFolderMenu(bool remove)
{
#ifdef Q_OS_WIN
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QStringList locations = {QStringLiteral("Directory/Background/shell/PiDesktop"),
                                   QStringLiteral("Directory/shell/PiDesktop")};
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    // 为目录参数追加反斜杠和点，避免磁盘根目录的尾反斜杠转义闭合引号。
    for (const QString &location : locations) {
        if (remove) {
            registry.remove(location);
        } else {
            const QString placeholder = location.contains(QStringLiteral("Background"))
                ? QStringLiteral("%V") : QStringLiteral("%1");
            registry.setValue(location + QStringLiteral("/Default"), QStringLiteral("在 Pi Desktop 中打开"));
            registry.setValue(location + QStringLiteral("/Icon"), QStringLiteral("\"%1\",0").arg(executable));
            registry.setValue(location + QStringLiteral("/command/Default"),
                              QStringLiteral("\"%1\" --workspace \"%2\\.\"").arg(executable, placeholder));
        }
    }
    registry.sync();
    const bool success = registry.status() == QSettings::NoError;
    if (success)
        qInfo() << "[FolderMenu] 操作成功；remove=" << remove << "executable=" << executable;
    else
        qCritical() << "[FolderMenu] 注册表写入失败；remove=" << remove << "status=" << registry.status();
    return success;
#else
    qWarning() << "[FolderMenu] 当前平台不支持 Windows 右键菜单；remove=" << remove;
    return false;
#endif
}

/**
 * 初始化 Qt Quick、处理目录启动参数并加载主 QML 窗口。
 */
int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PiDesktop"));
    QCoreApplication::setApplicationName(QStringLiteral("Pi Desktop"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pi Desktop"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("workspace"), QStringLiteral("以指定文件夹作为工作目录"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("install-folder-menu"), QStringLiteral("安装当前用户的文件夹右键菜单")});
    parser.addOption({QStringLiteral("uninstall-folder-menu"), QStringLiteral("移除当前用户的文件夹右键菜单")});
    parser.process(application);
    const bool installMenu = parser.isSet(QStringLiteral("install-folder-menu"));
    const bool uninstallMenu = parser.isSet(QStringLiteral("uninstall-folder-menu"));
    if ((installMenu && uninstallMenu) || !parser.positionalArguments().isEmpty()
        || ((installMenu || uninstallMenu) && parser.isSet(QStringLiteral("workspace")))) {
        qCritical() << "[FolderMenu] 启动参数无效或互斥";
        return EXIT_FAILURE;
    }
    if (installMenu || uninstallMenu)
        return configureFolderMenu(uninstallMenu) ? EXIT_SUCCESS : EXIT_FAILURE;

    QString workspace;
    if (parser.isSet(QStringLiteral("workspace"))) {
        const QString requested = parser.value(QStringLiteral("workspace"));
        const QFileInfo directory(QDir::fromNativeSeparators(requested));
        if (requested.trimmed().isEmpty() || !directory.isDir()
            || !QDir::setCurrent(directory.absoluteFilePath())) {
            qCritical() << "[FolderMenu] 工作目录无效或不可访问；path=" << requested;
            return EXIT_FAILURE;
        }
        workspace = QDir::currentPath();
        qInfo() << "[FolderMenu] 目录启动成功；cwd=" << workspace;
    } else {
        qInfo() << "[FolderMenu] 未指定目录，沿用已保存的工作目录";
    }

    // 在创建窗口前设置图标，保证标题栏、任务栏和切换窗口列表使用同一标识。
    const QIcon appIcon(QStringLiteral(":/assets/pi-desktop.png"));
    if (appIcon.isNull())
        qWarning() << "[AppIcon] 图标资源缺失或无效；path=:/assets/pi-desktop.png";
    else {
        QGuiApplication::setWindowIcon(appIcon);
        qInfo() << "[AppIcon] 应用图标加载成功；path=:/assets/pi-desktop.png";
    }

    // 必须在加载 QML 前选择可定制样式，避免 Windows 原生控件与自绘背景混合渲染。
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qInfo() << "[WorkspaceChrome] controls style initialized; style=" << QQuickStyle::name();

    AppController appController;
    // 控制器通过零延时定时器启动 RPC，必须在进入事件循环之前覆盖旧配置。
    if (!workspace.isEmpty())
        appController.settings()->setWorkspacePath(workspace);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &appController);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("PiDesktop"), QStringLiteral("Main"));

    return application.exec();
}
