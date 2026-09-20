#include "SessionModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>

Q_LOGGING_CATEGORY(sessionModelLog, "pidesktop.sessions")

/**
 * 初始化 Pi Session 列表模型。
 */
SessionModel::SessionModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_scanWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
        if (m_scanningRoot != m_sessionRoot) {
            // Profile 切换时旧扫描可能尚未结束，不允许旧结果回填到新列表。
            m_refreshPending = false;
            qCInfo(sessionModelLog) << "[ProfileSettings] 丢弃旧目录扫描结果:" << m_scanningRoot;
            refresh();
            return;
        }
        ScanResult result = m_scanWatcher.result();
        beginResetModel();
        m_sessions = std::move(result.sessions);
        m_cache = std::move(result.cache);
        endResetModel();
        emit countChanged();
        emit loadingChanged();
        emit refreshCompleted(m_sessions.size(), result.invalidCount);
        qCInfo(sessionModelLog) << "[SessionModel] 后台扫描完成，目录:" << m_sessionRoot
                                << "有效:" << m_sessions.size()
                                << "无效:" << result.invalidCount;

        if (m_refreshPending) {
            m_refreshPending = false;
            QTimer::singleShot(0, this, &SessionModel::refresh);
        }
    });
}

/**
 * 返回当前缓存的 Session 数量。
 */
int SessionModel::count() const
{
    return m_sessions.size();
}

/**
 * 查询 QFutureWatcher，供侧栏显示非阻塞加载状态。
 */
bool SessionModel::loading() const
{
    return m_scanWatcher.isRunning();
}

/**
 * SessionModel 是无子项的顶层列表。
 */
int SessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_sessions.size());
}

/**
 * 将 SessionInfo 字段映射到 QML。
 */
QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size()) {
        return {};
    }

    const SessionInfo &session = m_sessions.at(index.row());
    switch (role) {
    case SessionFolderRole: return QFileInfo(session.path).absolutePath();
    case IdRole: return session.id;
    case NameRole: return session.name;
    case PathRole: return session.path;
    case ProjectPathRole: return session.projectPath;
    case PreviewRole: return session.preview;
    case CreatedAtRole: return session.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case UpdatedAtRole: return session.updatedAt.toString(QStringLiteral("MM-dd HH:mm"));
    default: return {};
    }
}

/**
 * 声明稳定的 QML Session 角色名。
 */
QHash<int, QByteArray> SessionModel::roleNames() const
{
    return {
        {IdRole, "sessionId"},
        {NameRole, "sessionName"},
        {PathRole, "sessionPath"},
        {ProjectPathRole, "projectPath"},
        {PreviewRole, "preview"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
        {SessionFolderRole, "sessionFolder"}
    };
}

/**
 * 把磁盘扫描交给 QtConcurrent，避免大量历史 JSONL 阻塞 QML 渲染线程。
 */
void SessionModel::refresh()
{
    if (m_scanWatcher.isRunning()) {
        m_refreshPending = true;
        qCInfo(sessionModelLog) << "[SessionModel] 扫描正在运行，已合并后续刷新请求";
        return;
    }

    const QString root = m_sessionRoot.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                                   + QStringLiteral("/.pi/agent/sessions")
                             : m_sessionRoot;
    if (!QFileInfo(root).isDir()) {
        qCWarning(sessionModelLog) << "[SessionModel] Session 目录不存在:" << root;
    }

    qCInfo(sessionModelLog) << "[SessionModel] 开始后台扫描，目录:" << root;
    m_scanningRoot = m_sessionRoot;
    const QHash<QString, CacheEntry> previousCache = m_cache;
    m_scanWatcher.setFuture(QtConcurrent::run(
        [root, previousCache] { return scanSessions(root, previousCache); }));
    emit loadingChanged();
}

/**
 * 在工作线程递归收集 Session，按更新时间倒序排列，目录归属由项目导航负责。
 */
SessionModel::ScanResult SessionModel::scanSessions(
    const QString &root, const QHash<QString, CacheEntry> &previousCache)
{
    ScanResult result;
    if (!QFileInfo(root).isDir()) {
        return result;
    }

    QDirIterator iterator(root, {QStringLiteral("*.jsonl")}, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo fileInfo(iterator.next());
        const QString path = fileInfo.absoluteFilePath();
        const auto cached = previousCache.constFind(path);
        if (cached != previousCache.cend()
            && cached->size == fileInfo.size()
            && cached->modifiedAt == fileInfo.lastModified()) {
            result.sessions.append(cached->session);
            result.cache.insert(path, *cached);
            continue;
        }

        SessionInfo session;
        if (parseSessionFile(path, session)) {
            CacheEntry entry;
            entry.size = fileInfo.size();
            entry.modifiedAt = fileInfo.lastModified();
            entry.session = session;
            result.sessions.append(std::move(session));
            result.cache.insert(path, std::move(entry));
        } else {
            ++result.invalidCount;
        }
    }

    std::sort(result.sessions.begin(), result.sessions.end(),
              [](const SessionInfo &left, const SessionInfo &right) {
                  if (left.updatedAt != right.updatedAt)
                      return left.updatedAt > right.updatedAt;
                  return left.path < right.path;
              });
    qCInfo(sessionModelLog) << "[Projects] 历史按更新时间排序完成；会话数:"
                           << result.sessions.size() << "无效文件:" << result.invalidCount;
    return result;
}

/**
 * 保存 Profile 对应的 Session 目录，实际扫描由 refresh 显式触发。
 */
void SessionModel::setSessionRoot(const QString &sessionRoot)
{
    const QString normalized = QDir::cleanPath(sessionRoot);
    if (m_sessionRoot == normalized)
        return;
    m_sessionRoot = normalized;
    beginResetModel();
    m_sessions.clear();
    m_cache.clear();
    endResetModel();
    emit countChanged();
    qCInfo(sessionModelLog) << "[ProfileSettings] 会话目录已切换，旧缓存已清除:" << m_sessionRoot;
}

/**
 * 只读取文件头尾各 256 KiB 获取元数据，避免为侧栏解析完整长会话。
 */
bool SessionModel::parseSessionFile(const QString &path, SessionInfo &session)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument headerDocument = QJsonDocument::fromJson(file.readLine());
    if (!headerDocument.isObject()) {
        return false;
    }
    const QJsonObject header = headerDocument.object();
    if (header.value(QStringLiteral("type")).toString() != QStringLiteral("session")) {
        return false;
    }

    session.id = header.value(QStringLiteral("id")).toString();
    session.projectPath = header.value(QStringLiteral("cwd")).toString();
    session.createdAt = QDateTime::fromString(
        header.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);

    QString latestName;
    QString firstUserPreview;
    const auto consumeMetadataLine = [&latestName, &firstUserPreview](const QByteArray &line) {
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            return;
        }
        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("session_info")) {
            latestName = object.value(QStringLiteral("name")).toString();
        } else if (firstUserPreview.isEmpty() && type == QStringLiteral("message")) {
            const QJsonObject message = object.value(QStringLiteral("message")).toObject();
            if (message.value(QStringLiteral("role")).toString() == QStringLiteral("user")) {
                firstUserPreview = extractPreview(message.value(QStringLiteral("content")));
            }
        }
    };

    constexpr qint64 sectionSize = 256 * 1024;
    const qint64 headLimit = qMin(file.size(), sectionSize);
    while (!file.atEnd() && file.pos() < headLimit) {
        consumeMetadataLine(file.readLine());
    }

    // 长文件只补读末尾区域，以捕获后续重命名而不扫描中间的大量 Tool Output。
    if (!file.atEnd()) {
        const qint64 tailStart = qMax(file.pos(), file.size() - sectionSize);
        if (tailStart > file.pos()) {
            file.seek(tailStart);
            file.readLine();
        }
        while (!file.atEnd()) {
            consumeMetadataLine(file.readLine());
        }
    }

    const QFileInfo fileInfo(path);
    session.path = fileInfo.absoluteFilePath();
    session.updatedAt = fileInfo.lastModified();
    session.preview = firstUserPreview;
    session.name = latestName.trimmed();
    if (session.name.isEmpty()) {
        session.name = firstUserPreview.left(48).simplified();
    }
    if (session.name.isEmpty()) {
        session.name = tr("未命名会话");
    }
    return true;
}

/**
 * 兼容字符串和文本块数组，并限制侧栏预览长度。
 */
QString SessionModel::extractPreview(const QJsonValue &content)
{
    QString text;
    if (content.isString()) {
        text = content.toString();
    } else if (content.isArray()) {
        for (const QJsonValue &value : content.toArray()) {
            const QJsonObject block = value.toObject();
            if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                text += block.value(QStringLiteral("text")).toString();
            }
        }
    }
    return text.simplified().left(120);
}
