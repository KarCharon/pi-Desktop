#include "AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

/**
 * 以组织名和应用名创建平台原生配置存储。
 */
AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    // 显式遵守默认格式，让临时 INI 测试与用户注册表严格隔离。
    , m_settings(QSettings::defaultFormat(), QSettings::UserScope,
                 QStringLiteral("PiDesktop"), QStringLiteral("PiDesktop"))
{
    refreshDiscovery();
}

/**
 * 读取 Pi 可执行命令，默认从 PATH 查找 pi。
 */
QString AppSettings::piExecutable() const
{
    return m_settings.value(QStringLiteral("pi/executable"), QStringLiteral("pi")).toString();
}

/**
 * 仅在值变化时持久化 Pi 命令。
 */
void AppSettings::setPiExecutable(const QString &value)
{
    const QString normalized = value.trimmed().isEmpty() ? QStringLiteral("pi") : value.trimmed();
    if (normalized == piExecutable()) {
        return;
    }
    m_settings.setValue(QStringLiteral("pi/executable"), normalized);
    emit piExecutableChanged();
}

/**
 * 读取 Agent 工作目录，默认使用应用启动目录。
 */
QString AppSettings::workspacePath() const
{
    return m_settings.value(QStringLiteral("pi/workspace"), QDir::currentPath()).toString();
}

/**
 * 清理路径分隔符后持久化工作目录。
 */
void AppSettings::setWorkspacePath(const QString &value)
{
    const QString normalized = QDir::cleanPath(value.trimmed());
    if (normalized.isEmpty() || normalized == workspacePath()) {
        return;
    }
    m_settings.setValue(QStringLiteral("pi/workspace"), normalized);
    emit workspacePathChanged();
}

/**
 * 读取用户选择的 Profile；兼容未设置路径的旧版本配置。
 */
QString AppSettings::piProfilePath() const
{
    const QString defaultPath = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                                               + QStringLiteral("/.pi/profiles/Desktop"));
    return m_settings.value(QStringLiteral("pi/profilePath"), defaultPath).toString();
}

/** 将已校验的 Profile 路径持久化，下次启动继续使用。 */
void AppSettings::setPiProfilePath(const QString &value)
{
    if (value.trimmed().isEmpty())
        return;
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(value.trimmed()));
    if (!QDir::isAbsolutePath(normalized) || normalized == piProfilePath())
        return;
    m_settings.setValue(QStringLiteral("pi/profilePath"), normalized);
    emit piProfilePathChanged();
}

QString AppSettings::proxyUrl() const
{
    return m_settings.value(QStringLiteral("pi/proxyUrl")).toString();
}

bool AppSettings::isValidProxyUrl(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return true;
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && (url.scheme() == "http" || url.scheme() == "https")
        && !url.host().isEmpty() && url.port(1) > 0
        && (url.path().isEmpty() || url.path() == "/")
        && !url.hasQuery() && !url.hasFragment()
        && !text.contains('\n') && !text.contains('\r');
}

void AppSettings::setProxyUrl(const QString &value)
{
    const QString normalized = value.trimmed();
    if (!isValidProxyUrl(normalized) || normalized == proxyUrl())
        return;
    m_settings.setValue(QStringLiteral("pi/proxyUrl"), normalized);
    emit proxyUrlChanged();
}

/** 归一化有效目录并去重，仅检查目录元数据，不读取凭据或会话文件。 */
QStringList AppSettings::discoverProfiles(const QString &home, const QString &environmentDirectory,
                                          const QString &currentProfile, int *skipped)
{
    QStringList result;
    QSet<QString> seen;
    int rejected = 0;
    /** 累计无效目录并按真实路径去重，Windows 忽略大小写。 */
    const auto append = [&](const QString &path) {
        if (path.trimmed().isEmpty())
            return;
        const QFileInfo info(QDir::fromNativeSeparators(path));
        if (!info.isAbsolute() || !info.isDir() || !info.isReadable()) {
            ++rejected;
            return;
        }
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty()) {
            ++rejected;
            return;
        }
        QString key = canonical;
#ifdef Q_OS_WIN
        key = key.toCaseFolded();
#endif
        if (!seen.contains(key)) {
            seen.insert(key);
            result.append(canonical);
        }
    };
    append(currentProfile);
    append(environmentDirectory);
    QStringList roots;
    if (!home.isEmpty() && QDir::isAbsolutePath(home)) {
        append(QDir(home).filePath(QStringLiteral(".pi/agent")));
        roots.append(QDir(home).filePath(QStringLiteral(".pi/profiles")));
    }
    // 自定义位置若本身位于 profiles 下，也发现同级 Profile；禁止递归遍历磁盘。
    for (const QString &path : {environmentDirectory, currentProfile}) {
        if (!path.isEmpty() && QDir::isAbsolutePath(path)) {
            const QDir parent = QFileInfo(QDir::cleanPath(path)).dir();
            if (parent.dirName().compare(QStringLiteral("profiles"), Qt::CaseInsensitive) == 0)
                roots.append(parent.absolutePath());
        }
    }
    roots.removeDuplicates();
    for (const QString &root : roots) {
        const QFileInfo info(root);
        if (!info.isDir() || !info.isReadable()) {
            ++rejected;
            continue;
        }
        const QDir directory(root);
        const auto children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                                                       QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &child : children)
            append(child.absoluteFilePath());
    }
    if (skipped)
        *skipped = rejected;
    return result;
}

/** 返回已缓存的目录候选，避免 QML 属性读取反复扫描文件系统。 */
QStringList AppSettings::profileDirectories() const { return m_profileDirectories; }

/** 返回已缓存的 Pi 程序候选。 */
QStringList AppSettings::executablePaths() const { return m_executablePaths; }

/** 返回最近一次发现结果的可读摘要。 */
QString AppSettings::discoverySummary() const { return m_discoverySummary; }

/** 规范化候选键，目录已移除时仍能匹配原有隐藏记录。 */
static QString discoveryPathKey(const QString &path)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

/** 返回删除失败原因；成功时清空旧错误。 */
QString AppSettings::discoveryError() const { return m_discoveryError; }

/** 先持久化隐藏记录再更新候选；不触碰文件系统和正在使用的连接设置。 */
bool AppSettings::removeDiscoveryEntry(const QString &kind, const QString &path)
{
    m_discoveryError.clear();
    const bool profile = kind == QStringLiteral("profile");
    const bool executable = kind == QStringLiteral("executable");
    const auto &entries = profile ? m_profileDirectories : m_executablePaths;
    if ((!profile && !executable) || path.isEmpty() || !entries.contains(path)) {
        m_discoveryError = tr("选项已不存在或类型无效，请重新打开列表后重试。");
        emit discoveryErrorChanged();
        qWarning() << "[PiDiscovery] 删除被拒绝；kind=" << kind << "path=" << path
                   << "reason=invalid-or-stale-entry";
        return false;
    }
    const QString settingKey = QStringLiteral("discovery/hidden/") + kind;
    const QVariant previous = m_settings.value(settingKey);
    QStringList hidden = previous.toStringList();
    const QString key = discoveryPathKey(path);
    if (!hidden.contains(key))
        hidden.append(key);
    m_settings.setValue(settingKey, hidden);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        // 写入失败时还原内存中的配置，避免后续扫描错误地隐藏候选。
        if (previous.isValid())
            m_settings.setValue(settingKey, previous);
        else
            m_settings.remove(settingKey);
        m_discoveryError = tr("删除选项保存失败，请检查配置写入权限。");
        emit discoveryErrorChanged();
        qWarning() << "[PiDiscovery] 删除保存失败；kind=" << kind << "path=" << path
                   << "status=" << m_settings.status();
        return false;
    }
    emit discoveryErrorChanged();
    refreshDiscovery();
    qInfo() << "[PiDiscovery] 选项删除成功；kind=" << kind << "path=" << path
            << "diskDeleted=false; connectionChanged=false";
    return true;
}

/** 扫描用户标准路径和命令搜索路径，结果仅供选择，不覆盖已保存设置。 */
void AppSettings::refreshDiscovery()
{
    const QString home = QDir::homePath();
    const QString environmentDirectory = qEnvironmentVariable("PI_CODING_AGENT_DIR");
    int skipped = 0;
    m_profileDirectories = discoverProfiles(home, environmentDirectory, piProfilePath(), &skipped);
    QStringList searchPaths;
    // npm 的全局启动脚本在 Windows 通常位于 APPDATA/npm，即使未加入 PATH 也可发现。
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        searchPaths.prepend(QDir(appData).filePath(QStringLiteral("npm")));
#endif
    searchPaths.append(qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts));
    searchPaths.removeDuplicates();
    QStringList candidates{piExecutable()};
    const QStringList names = {
#ifdef Q_OS_WIN
        QStringLiteral("pi.cmd"), QStringLiteral("pi.exe"), QStringLiteral("pi.bat")
#else
        QStringLiteral("pi")
#endif
    };
    for (const QString &directory : searchPaths) {
        if (!QDir::isAbsolutePath(directory))
            continue;
        for (const QString &name : names) {
            const QString found = QStandardPaths::findExecutable(name, {directory});
            if (!found.isEmpty())
                candidates.append(found);
        }
    }
    m_executablePaths.clear();
    QSet<QString> seen;
    for (const QString &candidate : candidates) {
        const QString resolved = QStandardPaths::findExecutable(candidate);
        if (resolved.isEmpty())
            continue;
        const QString canonical = QFileInfo(resolved).canonicalFilePath();
        QString key = canonical;
#ifdef Q_OS_WIN
        key = key.toCaseFolded();
#endif
        if (!canonical.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            m_executablePaths.append(canonical);
        }
    }
    const QStringList hiddenProfiles = m_settings.value(QStringLiteral("discovery/hidden/profile")).toStringList();
    const QStringList hiddenExecutables = m_settings.value(QStringLiteral("discovery/hidden/executable")).toStringList();
    // 仅过滤最终候选，不把隐藏行为解释为删除目录或卸载程序。
    const auto hiddenProfileCount = m_profileDirectories.removeIf([&](const QString &path) {
        return hiddenProfiles.contains(discoveryPathKey(path));
    });
    const auto hiddenExecutableCount = m_executablePaths.removeIf([&](const QString &path) {
        return hiddenExecutables.contains(discoveryPathKey(path));
    });
    m_discoverySummary = tr("发现 %1 个 Pi 程序、%2 个 Profile；可输入或浏览自定义路径。")
        .arg(m_executablePaths.size()).arg(m_profileDirectories.size());
    emit discoveryChanged();
    qInfo() << "[PiDiscovery] 扫描完成；executables=" << m_executablePaths.size()
            << "profiles=" << m_profileDirectories.size() << "missingOrInvalid=" << skipped
            << "hiddenProfiles=" << hiddenProfileCount << "hiddenExecutables=" << hiddenExecutableCount
            << "environmentUnset=" << environmentDirectory.isEmpty()
            << "configuredProfile=" << piProfilePath();
    if (m_executablePaths.isEmpty())
        qWarning() << "[PiDiscovery] 无可见 Pi 程序（未安装、路径无效或已隐藏）；可自定义路径";
    if (m_profileDirectories.isEmpty())
        qWarning() << "[PiDiscovery] 无可见 Profile（目录缺失、无效或已隐藏）；可浏览选择已有目录";
}
