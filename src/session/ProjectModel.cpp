#include "ProjectModel.h"
#include "SessionModel.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QUuid>
#include <QUrl>

Q_LOGGING_CATEGORY(projectLog, "pidesktop.projects")

/** 从应用设置加载项目；配置与 Profile 路径完全独立。 */
ProjectModel::ProjectModel(SessionModel *sessions, QObject *parent)
    : QObject(parent), m_sessions(sessions)
{
    const QByteArray saved = QSettings().value(QStringLiteral("navigation/projects")).toByteArray();
    if (!saved.isEmpty()) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(saved, &parseError);
        bool valid = parseError.error == QJsonParseError::NoError && document.isArray();
        QSet<QString> ids;
        QSet<QString> directories;
        const auto projects = document.array().toVariantList();
        // 配置结构错误只输出一次汇总，不在逐项目校验中刷日志。
        for (const auto &value : projects) {
            const auto project = value.toMap();
            const QString id = project.value("id").toString();
            valid &= !id.isEmpty() && id != "ungrouped" && !ids.contains(id)
                     && !project.value("name").toString().trimmed().isEmpty()
                     && project.value("folders").metaType() == QMetaType::fromType<QVariantList>();
            ids.insert(id);
            for (const auto &entry : project.value("folders").toList()) {
                const auto folder = entry.toMap();
                const QString fid = folder.value("id").toString();
                const QString path = folder.value("path").toString();
                valid &= !fid.isEmpty() && !ids.contains(fid) && !path.isEmpty()
                         && QDir::isAbsolutePath(path) && !directories.contains(pathKey(path))
                         && !folder.value("name").toString().trimmed().isEmpty();
                ids.insert(fid);
                directories.insert(pathKey(path));
            }
        }
        if (valid)
            m_projects = projects;
        else
            fail(tr("项目配置无效，原配置未覆盖，请检查应用设置。"));
    }
    connect(sessions, &QAbstractItemModel::modelReset, this, &ProjectModel::rebuild);
    rebuild();
    qCInfo(projectLog) << "[Projects] 已加载；项目数:" << m_projects.size()
                      << "配置缺省:" << saved.isEmpty();
}

/** 返回可供 QML 虚拟化显示的扁平树。 */
QVariantList ProjectModel::rows() const { return m_rows; }

/** 返回最近一次项目操作错误。 */
QString ProjectModel::error() const { return m_error; }

/** 将目录路径转为可比较的规范键，不让空路径落到当前目录。 */
QString ProjectModel::pathKey(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString canonical = QFileInfo(key).canonicalFilePath();
    if (!canonical.isEmpty())
        key = canonical;
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

/** 统一输出用户可见错误和低频诊断日志。 */
bool ProjectModel::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    qCWarning(projectLog) << "[Projects] 操作失败:" << message;
    return false;
}

/** 先确认配置落盘，再更新界面，避免保存失败时误报成功。 */
bool ProjectModel::persist(const QVariantList &projects)
{
    QSettings settings;
    settings.setValue(QStringLiteral("navigation/projects"),
                      QJsonDocument(QJsonArray::fromVariantList(projects)).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return fail(tr("项目配置保存失败，请检查应用设置的写入权限。"));
    m_projects = projects;
    m_error.clear();
    emit errorChanged();
    rebuild();
    qCInfo(projectLog) << "[Projects] 配置保存成功；项目数:" << projects.size();
    return true;
}

/** 使用稳定编号新建或改名，避免改名导致会话归属变化。 */
bool ProjectModel::saveProject(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return fail(tr("项目名称不能为空。"));
    auto projects = m_projects;
    if (id.isEmpty()) {
        projects.append(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                    {"name", name.trimmed()}, {"folders", QVariantList{}}});
        return persist(projects);
    }
    for (auto &value : projects) {
        auto project = value.toMap();
        if (project.value("id").toString() != id)
            continue;
        project["name"] = name.trimmed();
        value = project;
        return persist(projects);
    }
    return fail(tr("项目不存在，请刷新后重试。"));
}

/** 校验真实目录并防止同一个工作目录重复归属不同项目。 */
bool ProjectModel::addFolder(const QString &projectId, const QString &path, const QString &name)
{
    const QUrl url(path);
    const QString local = QDir::fromNativeSeparators(url.isLocalFile() ? url.toLocalFile() : path.trimmed());
    if (local.isEmpty() || !QDir::isAbsolutePath(local) || !QFileInfo(local).isDir())
        return fail(tr("请选择已存在的绝对工作目录。"));
    const QString key = pathKey(local);
    bool duplicate = false;
    for (const auto &value : m_projects)
        for (const auto &folder : value.toMap().value("folders").toList())
            duplicate |= pathKey(folder.toMap().value("path").toString()) == key;
    if (duplicate)
        return fail(tr("该工作文件夹已经加入项目，请勿重复添加。"));
    auto projects = m_projects;
    for (auto &value : projects) {
        auto project = value.toMap();
        if (project.value("id").toString() != projectId)
            continue;
        auto folders = project.value("folders").toList();
        QString title = name.trimmed().isEmpty() ? QFileInfo(local).fileName() : name.trimmed();
        if (title.isEmpty())
            title = QDir::cleanPath(local);
        folders.append(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                   {"name", title}, {"path", QDir::cleanPath(local)}});
        project["folders"] = folders;
        value = project;
        return persist(projects);
    }
    return fail(tr("目标项目不存在。"));
}

/** 只更新显示别名，真实工作目录和历史会话保持不变。 */
bool ProjectModel::renameFolder(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return fail(tr("文件夹名称不能为空。"));
    auto projects = m_projects;
    for (auto &value : projects) {
        auto project = value.toMap();
        auto folders = project.value("folders").toList();
        for (auto &entry : folders) {
            auto folder = entry.toMap();
            if (folder.value("id").toString() != id)
                continue;
            folder["name"] = name.trimmed();
            entry = folder;
            project["folders"] = folders;
            value = project;
            return persist(projects);
        }
    }
    return fail(tr("工作文件夹不存在。"));
}

/** 从导航树移除项目或文件夹，遗留会话会显示在未分组下。 */
bool ProjectModel::removeEntry(const QString &id)
{
    auto projects = m_projects;
    for (qsizetype i = 0; i < projects.size(); ++i) {
        auto project = projects[i].toMap();
        if (project.value("id").toString() == id) {
            projects.removeAt(i);
            return persist(projects);
        }
        auto folders = project.value("folders").toList();
        for (qsizetype j = 0; j < folders.size(); ++j) {
            if (folders[j].toMap().value("id").toString() != id)
                continue;
            folders.removeAt(j);
            project["folders"] = folders;
            projects[i] = project;
            return persist(projects);
        }
    }
    return fail(tr("要移除的项目或文件夹不存在。"));
}

/** 按真实 cwd 精确分组，不读取或展示 Profile 中的会话存储目录名。 */
void ProjectModel::rebuild()
{
    QMap<QString, QVariantList> byDirectory;
    QMap<QString, QString> paths;
    for (int i = 0; i < m_sessions->rowCount(); ++i) {
        const auto index = m_sessions->index(i);
        const QString path = m_sessions->data(index, SessionModel::ProjectPathRole).toString();
        const QString key = pathKey(path);
        paths[key] = path;
        byDirectory[key].append(QVariantMap{
            {"kind", "session"}, {"id", m_sessions->data(index, SessionModel::PathRole)},
            {"title", m_sessions->data(index, SessionModel::NameRole)},
            {"sessionPath", m_sessions->data(index, SessionModel::PathRole)},
            {"path", path}, {"preview", m_sessions->data(index, SessionModel::PreviewRole)},
            {"updatedAt", m_sessions->data(index, SessionModel::UpdatedAtRole)}});
    }
    m_rows.clear();
    // 搜索文本带上祖先与子会话内容，搜索时不留下没有匹配项的标题。
    const auto appendProject = [this, &byDirectory](const QVariantMap &project) {
        const QString pid = project.value("id").toString();
        const QString name = project.value("name").toString();
        const qsizetype projectRow = m_rows.size();
        m_rows.append(QVariantMap{{"kind", "project"}, {"id", pid}, {"title", name},
                                  {"projectId", pid}, {"managed", pid != "ungrouped"}});
        QString projectSearch = name;
        for (const auto &value : project.value("folders").toList()) {
            const auto folder = value.toMap();
            const QString fid = folder.value("id").toString();
            const QString path = folder.value("path").toString();
            const QString title = folder.value("name").toString();
            const QString baseSearch = name + ' ' + title + ' ' + path;
            QString folderSearch = baseSearch;
            const qsizetype folderRow = m_rows.size();
            m_rows.append(QVariantMap{{"kind", "folder"}, {"id", fid}, {"title", title},
                                      {"path", path}, {"projectId", pid}, {"folderId", fid},
                                      {"managed", pid != "ungrouped"}});
            for (const auto &session : byDirectory.take(pathKey(path))) {
                auto row = session.toMap();
                row["projectId"] = pid;
                row["folderId"] = fid;
                row["searchText"] = baseSearch + ' ' + row.value("title").toString()
                                     + ' ' + row.value("preview").toString();
                folderSearch += ' ' + row.value("searchText").toString();
                m_rows.append(row);
            }
            auto row = m_rows[folderRow].toMap();
            row["searchText"] = folderSearch;
            m_rows[folderRow] = row;
            projectSearch += ' ' + folderSearch;
        }
        auto row = m_rows[projectRow].toMap();
        row["searchText"] = projectSearch;
        m_rows[projectRow] = row;
    };
    for (const auto &project : m_projects)
        appendProject(project.toMap());
    QVariantList ungrouped;
    for (auto it = byDirectory.cbegin(); it != byDirectory.cend(); ++it) {
        const QString path = paths.value(it.key());
        QString name = QFileInfo(path).fileName();
        if (name.isEmpty())
            name = path.isEmpty() ? tr("未知工作目录") : path;
        ungrouped.append(QVariantMap{{"id", "unmanaged:" + it.key()}, {"name", name}, {"path", path}});
    }
    if (!ungrouped.isEmpty())
        appendProject(QVariantMap{{"id", "ungrouped"}, {"name", tr("未分组")}, {"folders", ungrouped}});
    emit rowsChanged();
    qCInfo(projectLog) << "[Projects] 导航更新成功；会话:" << m_sessions->count()
                      << "行数:" << m_rows.size() << "未分组目录:" << ungrouped.size();
}
