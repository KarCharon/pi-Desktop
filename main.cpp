#include "app/AppController.h"
#include "config/AppSettings.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStyleHints>

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
 * 构建浅色或深色应用调色板。
 *
 * QML 侧已经逐项自绘缓冲色，这里的调色板只作用于尚未自绘的 Basic 控件
 * （下拉框、进度条、按钮文字等），并决定 Windows 上系统光标的明暗版本。
 */
static QPalette buildUiPalette(bool dark)
{
    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1b1917")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e6e1d8")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#242220")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#201e1b")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#d3cdc3")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#2a2724")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e6e1d8")));
        palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#242220")));
        palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e6e1d8")));
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6d675f")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#5b5650")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#5b5650")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#5b5650")));
    } else {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f7f5f0")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#302d29")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f3f0e9")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#403d37")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#f7f4ef")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#403d37")));
        palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#fbfaf7")));
        palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#302d29")));
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#aaa49b")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#b9b1a6")));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#b9b1a6")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#b9b1a6")));
    }
    // 选中高亮在深浅主题下都使用暖橙，保持品牌一致。
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#c47a59")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    return palette;
}

/**
 * 应用浅色/深色主题：同步应用调色板与系统配色方案。
 *
 * 必须显式声明配色方案，否则 Windows 在深色模式下会让 Qt 选取白色 I 型
 * 文本光标，落在浅色自绘背景上就几乎看不见。
 */
static void applyUiTheme(const QString &theme)
{
    const bool dark = AppSettings::isDarkTheme(theme);
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    // setColorScheme 可能先重置调色板，因此随后再显式覆盖一次。
    QGuiApplication::setPalette(buildUiPalette(dark));
    qInfo() << "[UiTheme] applied; theme=" << theme << "dark=" << dark;
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

    // 主题要在加载 QML 之前应用，避免首帧闪出与设置不符的配色与系统光标。
    QObject::connect(appController.settings(), &AppSettings::uiThemeChanged,
                     &application, [&appController] {
        applyUiTheme(appController.settings()->uiTheme());
    });
    applyUiTheme(appController.settings()->uiTheme());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &appController);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("PiDesktop"), QStringLiteral("Main"));

    return application.exec();
}
