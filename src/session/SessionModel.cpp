#include "SessionModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>

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
        if (m_usageProfileRoot == m_sessionRoot) {
            m_usageCache = m_cache;
            m_tokenUsageUpdatedAt = QDateTime::currentDateTime();
        }
        rebuildTokenUsageView();
        emit countChanged();
        emit loadingChanged();
        emit refreshCompleted(m_sessions.size(), result.invalidCount);
        qCInfo(sessionModelLog) << "[SessionModel] 后台扫描完成，目录:" << m_sessionRoot
                                << "有效:" << m_sessions.size()
                                << "无效:" << result.invalidCount;
        qCInfo(sessionModelLog) << "[TokenUsage] 聚合成功；文件=" << result.usageFileCount
                                << "事件=" << result.usageEventCount
                                << "tokens=" << result.totalTokens
                                << "跳过=" << result.skippedUsageCount;

        if (m_refreshPending) {
            m_refreshPending = false;
            QTimer::singleShot(0, this, &SessionModel::refresh);
        }
    });

    connect(&m_usageScanWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
        if (m_usageScanningRoot != m_usageProfileRoot) {
            // 统计筛选在后台切换时丢弃旧目录结果，避免旧 Profile 覆盖新选择。
            m_usageRefreshPending = false;
            qCInfo(sessionModelLog) << "[TokenUsage] 丢弃旧 Profile 扫描结果；目录=" << m_usageScanningRoot;
            if (m_usageProfileRoot == m_sessionRoot) {
                m_usageCache = m_cache;
                m_tokenUsageUpdatedAt = QDateTime::currentDateTime();
                rebuildTokenUsageView();
            } else {
                startTokenUsageScan();
            }
            return;
        }
        const ScanResult result = m_usageScanWatcher.result();
        m_usageCache = result.cache;
        m_tokenUsageUpdatedAt = QDateTime::currentDateTime();
        rebuildTokenUsageView();
        qCInfo(sessionModelLog) << "[TokenUsage] Profile 筛选扫描完成；目录=" << m_usageProfileRoot
                                << "文件=" << result.usageFileCount
                                << "事件=" << result.usageEventCount
                                << "tokens=" << result.totalTokens
                                << "跳过=" << result.skippedUsageCount;
        if (m_usageRefreshPending) {
            m_usageRefreshPending = false;
            QTimer::singleShot(0, this, &SessionModel::startTokenUsageScan);
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
    return m_scanWatcher.isRunning() || m_usageScanWatcher.isRunning();
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
        if (m_usageProfileRoot != m_sessionRoot) {
            if (m_usageScanWatcher.isRunning())
                m_usageRefreshPending = true;
            else
                startTokenUsageScan();
        }
        qCInfo(sessionModelLog) << "[SessionModel] 扫描正在运行，已合并后续刷新请求";
        return;
    }

    const QString root = m_sessionRoot.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                                   + QStringLiteral("/.pi/agent/sessions")
                             : m_sessionRoot;
    if (!QFileInfo(root).isDir()) {
        qCWarning(sessionModelLog) << "[SessionModel] Session 目录不存在:" << root;
        qCWarning(sessionModelLog) << "[TokenUsage] 跳过聚合；原因=session-directory-missing；目录=" << root;
    }

    qCInfo(sessionModelLog) << "[SessionModel] 开始后台扫描，目录:" << root;
    qCInfo(sessionModelLog) << "[TokenUsage] 开始后台聚合；周期=" << m_tokenUsagePeriod;
    m_scanningRoot = m_sessionRoot;
    const QHash<QString, CacheEntry> previousCache = m_cache;
    m_scanWatcher.setFuture(QtConcurrent::run(
        [root, previousCache] { return scanSessions(root, previousCache); }));
    if (m_usageProfileRoot != m_sessionRoot)
        startTokenUsageScan();
    else
        rebuildTokenUsageView();
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
            parseSessionUsage(path, cached != previousCache.cend() ? &(*cached) : nullptr, entry);
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
    for (auto cache = result.cache.cbegin(); cache != result.cache.cend(); ++cache) {
        int fileEvents = 0;
        qint64 fileTokens = 0;
        for (auto bucket = cache->usageByHour.cbegin(); bucket != cache->usageByHour.cend(); ++bucket) {
            fileEvents += bucket->eventCount;
            fileTokens += bucket->input + bucket->output + bucket->cacheRead + bucket->cacheWrite;
        }
        if (fileEvents > 0)
            ++result.usageFileCount;
        result.usageEventCount += fileEvents;
        result.totalTokens += fileTokens;
        result.skippedUsageCount += cache->skippedUsageCount;
    }
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
    const QString previousRoot = m_sessionRoot;
    m_sessionRoot = normalized;
    if (m_usageProfileRoot.isEmpty() || m_usageProfileRoot == previousRoot) {
        m_usageProfileRoot = normalized;
        m_usageCache.clear();
    }
    beginResetModel();
    m_sessions.clear();
    m_cache.clear();
    m_tokenUsageOffset = 0;
    m_tokenUsageError.clear();
    m_tokenUsageUpdatedAt = {};
    endResetModel();
    rebuildTokenUsageView();
    emit countChanged();
    qCInfo(sessionModelLog) << "[ProfileSettings] 会话目录已切换，旧缓存已清除:" << m_sessionRoot;
    qCInfo(sessionModelLog) << "[TokenUsage] Profile 切换，统计缓存已清除；目录=" << m_sessionRoot;
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

/**
 * 校验 usage 数值并累加到本地时间对应的小时桶。
 */
bool SessionModel::addUsageRecord(const QJsonObject &usage, const QDateTime &timestamp,
                                  QHash<QString, UsageTotals> &buckets)
{
    if (!timestamp.isValid())
        return false;

    const auto tokenValue = [](const QJsonValue &value) -> qint64 {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number <= 0.0)
            return 0;
        const double maximum = static_cast<double>(std::numeric_limits<qint64>::max());
        return static_cast<qint64>(std::min(number, maximum));
    };

    const qint64 input = tokenValue(usage.value(QStringLiteral("input")));
    const qint64 output = tokenValue(usage.value(QStringLiteral("output")));
    const qint64 cacheRead = tokenValue(usage.value(QStringLiteral("cacheRead")));
    const qint64 cacheWrite = tokenValue(usage.value(QStringLiteral("cacheWrite")));
    const QJsonValue costValue = usage.value(QStringLiteral("cost"));
    const double rawCost = costValue.isObject()
                               ? costValue.toObject().value(QStringLiteral("total")).toDouble()
                               : costValue.toDouble();
    const double cost = std::isfinite(rawCost) && rawCost > 0.0 ? rawCost : 0.0;
    if (input == 0 && output == 0 && cacheRead == 0 && cacheWrite == 0 && cost == 0.0)
        return false;

    const QString hourKey = timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd'T'HH"));
    UsageTotals &totals = buckets[hourKey];
    totals.input += input;
    totals.output += output;
    totals.cacheRead += cacheRead;
    totals.cacheWrite += cacheWrite;
    totals.cost += cost;
    ++totals.eventCount;
    return true;
}

/**
 * 对增长中的 JSONL 文件从上次完整行边界继续解析；重写或缩小时从头重建。
 */
void SessionModel::parseSessionUsage(const QString &path, const CacheEntry *previous,
                                     CacheEntry &entry)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ++entry.skippedUsageCount;
        return;
    }

    qint64 startOffset = 0;
    if (previous && previous->session.id == entry.session.id
        && entry.size > previous->size
        && previous->parsedUsageBytes >= 0
        && previous->parsedUsageBytes <= previous->size) {
        startOffset = previous->parsedUsageBytes;
        entry.usageByHour = previous->usageByHour;
        entry.skippedUsageCount = previous->skippedUsageCount;
    }
    if (!file.seek(startOffset)) {
        startOffset = 0;
        entry.usageByHour.clear();
        entry.skippedUsageCount = 0;
        file.seek(0);
    }
    entry.parsedUsageBytes = startOffset;

    // Pi 优先记录消息毫秒时间戳，旧会话则回退到条目的 ISO 时间戳。
    const auto parseTimestamp = [](const QJsonValue &value) -> QDateTime {
        if (value.isDouble()) {
            const double milliseconds = value.toDouble();
            if (std::isfinite(milliseconds) && milliseconds > 0.0)
                return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds));
        }
        if (value.isString()) {
            QDateTime timestamp = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
            if (!timestamp.isValid())
                timestamp = QDateTime::fromString(value.toString(), Qt::ISODate);
            return timestamp;
        }
        return {};
    };

    while (!file.atEnd()) {
        const qint64 lineStart = file.pos();
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty()) {
            entry.parsedUsageBytes = file.pos();
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            // 未以换行结束的无效尾段可能仍在写入，下次从原位置重试。
            if (!line.endsWith('\n')) {
                entry.parsedUsageBytes = lineStart;
                break;
            }
            ++entry.skippedUsageCount;
            entry.parsedUsageBytes = file.pos();
            continue;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        QJsonObject usage;
        QDateTime timestamp;
        if (type == QStringLiteral("message")) {
            const QJsonObject message = object.value(QStringLiteral("message")).toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            if ((role == QStringLiteral("assistant") || role == QStringLiteral("toolResult"))
                && message.value(QStringLiteral("usage")).isObject()) {
                usage = message.value(QStringLiteral("usage")).toObject();
                timestamp = parseTimestamp(message.value(QStringLiteral("timestamp")));
            }
        } else if ((type == QStringLiteral("compaction") || type == QStringLiteral("branch_summary"))
                   && object.value(QStringLiteral("usage")).isObject()) {
            usage = object.value(QStringLiteral("usage")).toObject();
        }
        if (!usage.isEmpty()) {
            if (!timestamp.isValid())
                timestamp = parseTimestamp(object.value(QStringLiteral("timestamp")));
            if (!addUsageRecord(usage, timestamp, entry.usageByHour))
                ++entry.skippedUsageCount;
        }
        entry.parsedUsageBytes = file.pos();
    }
}

/**
 * 返回当前供右侧栏消费的不可变统计快照。
 */
QVariantMap SessionModel::tokenUsageView() const
{
    return m_tokenUsageView;
}

/**
 * 返回当前选中的 Token 统计 Profile。
 */
QString SessionModel::tokenUsageProfile() const
{
    return m_tokenUsageProfile;
}

/**
 * 返回可供 Token 统计筛选的 Profile 列表。
 */
QStringList SessionModel::tokenUsageProfiles() const
{
    return m_tokenUsageProfiles;
}

/**
 * 更新 Token 统计 Profile 候选，并保留当前已选项。
 */
void SessionModel::setTokenUsageProfiles(const QStringList &profiles)
{
    QStringList normalized;
    for (const QString &profile : profiles) {
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(profile.trimmed()));
        if (!path.isEmpty() && QDir::isAbsolutePath(path) && !normalized.contains(path))
            normalized.append(path);
    }
    if (normalized == m_tokenUsageProfiles)
        return;
    m_tokenUsageProfiles = normalized;
    emit tokenUsageProfilesChanged();
    qCInfo(sessionModelLog) << "[TokenUsage] Profile 筛选候选已更新；数量=" << normalized.size();
}

/**
 * 切换 Token 统计 Profile，只重扫统计目录，不改变活动 Session 列表。
 */
bool SessionModel::setTokenUsageProfile(const QString &profile)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(profile.trimmed()));
    if (!QDir::isAbsolutePath(normalized) || !QFileInfo(normalized).isDir()) {
        m_tokenUsageError = tr("Token 统计 Profile 目录无效。" );
        rebuildTokenUsageView();
        qCWarning(sessionModelLog) << "[TokenUsage] Profile 筛选失败；原因=invalid-profile；目录=" << profile;
        return false;
    }
    const QString usageRoot = QDir(normalized).filePath(QStringLiteral("sessions"));
    const bool changed = m_tokenUsageProfile != normalized;
    m_tokenUsageProfile = normalized;
    m_usageProfileRoot = usageRoot;
    m_usageCache.clear();
    m_tokenUsageUpdatedAt = {};
    m_tokenUsageError.clear();
    if (changed)
        emit tokenUsageProfileChanged();
    rebuildTokenUsageView();
    if (m_usageProfileRoot == m_sessionRoot) {
        m_usageCache = m_cache;
        m_tokenUsageUpdatedAt = QDateTime::currentDateTime();
        rebuildTokenUsageView();
    } else {
        startTokenUsageScan();
    }
    qCInfo(sessionModelLog) << "[TokenUsage] Profile 筛选已切换；目录=" << m_tokenUsageProfile;
    return true;
}

/**
 * 启动所选非活动 Profile 的 Token JSONL 增量扫描。
 */
void SessionModel::startTokenUsageScan()
{
    if (m_usageProfileRoot == m_sessionRoot) {
        m_usageCache = m_cache;
        rebuildTokenUsageView();
        return;
    }
    if (m_usageScanWatcher.isRunning()) {
        m_usageRefreshPending = true;
        return;
    }
    const QString root = m_usageProfileRoot;
    if (root.isEmpty()) {
        m_usageCache.clear();
        rebuildTokenUsageView();
        return;
    }
    qCInfo(sessionModelLog) << "[TokenUsage] 开始扫描筛选 Profile；目录=" << root;
    m_usageScanningRoot = root;
    const QHash<QString, CacheEntry> previousCache = m_usageCache;
    m_usageScanWatcher.setFuture(QtConcurrent::run(
        [root, previousCache] { return scanSessions(root, previousCache); }));
    rebuildTokenUsageView();
}

/**
 * 切换预定义周期并回到包含今天的当前周期。
 */
void SessionModel::setTokenUsagePeriod(const QString &period)
{
    const QString normalized = period.trimmed().toLower();
    static const QSet<QString> supported {
        QStringLiteral("day"), QStringLiteral("week"), QStringLiteral("month"),
        QStringLiteral("total"), QStringLiteral("custom")
    };
    if (!supported.contains(normalized)) {
        m_tokenUsageError = tr("不支持的统计周期：%1").arg(period);
        rebuildTokenUsageView();
        qCWarning(sessionModelLog) << "[TokenUsage] 周期切换失败；原因=invalid-period；值=" << period;
        return;
    }
    if (normalized == QStringLiteral("custom")
        && (!m_customUsageFrom.isValid() || !m_customUsageTo.isValid())) {
        m_tokenUsageError = tr("请先选择有效的自定义起止日期。");
        rebuildTokenUsageView();
        qCWarning(sessionModelLog) << "[TokenUsage] 周期切换失败；原因=custom-range-missing";
        return;
    }
    m_tokenUsagePeriod = normalized;
    m_tokenUsageOffset = 0;
    m_tokenUsageError.clear();
    rebuildTokenUsageView();
    qCInfo(sessionModelLog) << "[TokenUsage] 周期已切换；period=" << m_tokenUsagePeriod;
}

/**
 * 移动当前预定义周期，禁止进入包含今天周期之后的未来范围。
 */
void SessionModel::shiftTokenUsagePeriod(int amount)
{
    if (amount == 0)
        return;
    if (m_tokenUsagePeriod == QStringLiteral("total")
        || m_tokenUsagePeriod == QStringLiteral("custom")) {
        qCInfo(sessionModelLog) << "[TokenUsage] 跳过周期移动；原因=fixed-range；period="
                                << m_tokenUsagePeriod;
        return;
    }
    const int nextOffset = qMin(0, m_tokenUsageOffset + amount);
    if (nextOffset == m_tokenUsageOffset) {
        qCInfo(sessionModelLog) << "[TokenUsage] 跳过周期移动；原因=future-range";
        return;
    }
    m_tokenUsageOffset = nextOffset;
    m_tokenUsageError.clear();
    rebuildTokenUsageView();
    qCInfo(sessionModelLog) << "[TokenUsage] 周期已移动；period=" << m_tokenUsagePeriod
                            << "offset=" << m_tokenUsageOffset;
}

/**
 * 解析 ISO 日期并应用包含首尾两天的自定义范围。
 */
bool SessionModel::setCustomTokenUsageRange(const QString &from, const QString &to)
{
    const QDate fromDate = QDate::fromString(from.trimmed(), Qt::ISODate);
    const QDate toDate = QDate::fromString(to.trimmed(), Qt::ISODate);
    if (!fromDate.isValid() || !toDate.isValid() || fromDate > toDate) {
        m_tokenUsageError = tr("日期格式应为 YYYY-MM-DD，且开始日期不能晚于结束日期。");
        rebuildTokenUsageView();
        qCWarning(sessionModelLog) << "[TokenUsage] 自定义范围失败；原因=invalid-date-range；from="
                                   << from << "to=" << to;
        return false;
    }
    m_customUsageFrom = fromDate;
    m_customUsageTo = toDate;
    m_tokenUsagePeriod = QStringLiteral("custom");
    m_tokenUsageOffset = 0;
    m_tokenUsageError.clear();
    rebuildTokenUsageView();
    qCInfo(sessionModelLog) << "[TokenUsage] 自定义范围已应用；from=" << fromDate
                            << "to=" << toDate;
    return true;
}

/**
 * 按小时、日或月组织趋势点，同时计算范围内去重的 Session 数量。
 */
void SessionModel::rebuildTokenUsageView()
{
    const QDate today = QDate::currentDate();
    QDate minimumDate;
    QDate maximumDate;
    for (auto cache = m_usageCache.cbegin(); cache != m_usageCache.cend(); ++cache) {
        for (auto bucket = cache->usageByHour.cbegin(); bucket != cache->usageByHour.cend(); ++bucket) {
            const QDate date = QDate::fromString(bucket.key().left(10), Qt::ISODate);
            if (!date.isValid())
                continue;
            if (!minimumDate.isValid() || date < minimumDate)
                minimumDate = date;
            if (!maximumDate.isValid() || date > maximumDate)
                maximumDate = date;
        }
    }

    QDate fromDate;
    QDate toDate;
    if (m_tokenUsagePeriod == QStringLiteral("day")) {
        fromDate = today.addDays(m_tokenUsageOffset);
        toDate = fromDate;
    } else if (m_tokenUsagePeriod == QStringLiteral("week")) {
        fromDate = today.addDays(1 - today.dayOfWeek()).addDays(m_tokenUsageOffset * 7);
        toDate = fromDate.addDays(6);
    } else if (m_tokenUsagePeriod == QStringLiteral("month")) {
        fromDate = QDate(today.year(), today.month(), 1).addMonths(m_tokenUsageOffset);
        toDate = fromDate.addMonths(1).addDays(-1);
    } else if (m_tokenUsagePeriod == QStringLiteral("custom")) {
        fromDate = m_customUsageFrom.isValid() ? m_customUsageFrom : today;
        toDate = m_customUsageTo.isValid() ? m_customUsageTo : today;
    } else {
        fromDate = minimumDate.isValid() ? minimumDate : today;
        toDate = maximumDate.isValid() ? maximumDate : today;
    }

    const bool hourly = m_tokenUsagePeriod == QStringLiteral("day");
    const bool monthly = m_tokenUsagePeriod == QStringLiteral("total")
                         || (m_tokenUsagePeriod == QStringLiteral("custom")
                             && fromDate.daysTo(toDate) > 62);
    QMap<QString, UsageTotals> grouped;
    if (hourly) {
        for (int hour = 0; hour < 24; ++hour)
            grouped.insert(QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0')), {});
    } else if (monthly) {
        QDate month(fromDate.year(), fromDate.month(), 1);
        const QDate lastMonth(toDate.year(), toDate.month(), 1);
        while (month <= lastMonth) {
            grouped.insert(month.toString(QStringLiteral("yyyy-MM")), {});
            month = month.addMonths(1);
        }
    } else {
        for (QDate date = fromDate; date <= toDate; date = date.addDays(1))
            grouped.insert(date.toString(Qt::ISODate), {});
    }

    UsageTotals summary;
    int activeSessions = 0;
    const auto accumulate = [](UsageTotals &target, const UsageTotals &source) {
        target.input += source.input;
        target.output += source.output;
        target.cacheRead += source.cacheRead;
        target.cacheWrite += source.cacheWrite;
        target.cost += source.cost;
        target.eventCount += source.eventCount;
    };
    for (auto cache = m_usageCache.cbegin(); cache != m_usageCache.cend(); ++cache) {
        bool sessionActive = false;
        for (auto bucket = cache->usageByHour.cbegin(); bucket != cache->usageByHour.cend(); ++bucket) {
            const QDateTime hour = QDateTime::fromString(bucket.key(), QStringLiteral("yyyy-MM-dd'T'HH"));
            const QDate date = hour.date();
            if (!date.isValid() || date < fromDate || date > toDate)
                continue;
            sessionActive = true;
            QString groupKey;
            if (hourly)
                groupKey = QStringLiteral("%1").arg(hour.time().hour(), 2, 10, QLatin1Char('0'));
            else if (monthly)
                groupKey = date.toString(QStringLiteral("yyyy-MM"));
            else
                groupKey = date.toString(Qt::ISODate);
            accumulate(grouped[groupKey], bucket.value());
            accumulate(summary, bucket.value());
        }
        if (sessionActive)
            ++activeSessions;
    }

    QVariantList points;
    for (auto point = grouped.cbegin(); point != grouped.cend(); ++point) {
        QString label = point.key();
        if (hourly)
            label += QStringLiteral(":00");
        else if (monthly) {
            const QDate date = QDate::fromString(point.key() + QStringLiteral("-01"), Qt::ISODate);
            label = date.isValid() ? date.toString(QStringLiteral("yyyy/M")) : point.key();
        } else {
            const QDate date = QDate::fromString(point.key(), Qt::ISODate);
            label = date.isValid() ? date.toString(QStringLiteral("M/d")) : point.key();
        }
        points.append(QVariantMap {
            {QStringLiteral("key"), point.key()},
            {QStringLiteral("label"), label},
            {QStringLiteral("total"), point->input + point->output + point->cacheRead + point->cacheWrite},
            {QStringLiteral("cost"), point->cost}
        });
    }

    QString rangeLabel;
    if (m_tokenUsagePeriod == QStringLiteral("day"))
        rangeLabel = fromDate.toString(QStringLiteral("yyyy年M月d日"));
    else if (m_tokenUsagePeriod == QStringLiteral("week"))
        rangeLabel = tr("%1 - %2").arg(fromDate.toString(QStringLiteral("M月d日")),
                                      toDate.toString(QStringLiteral("M月d日")));
    else if (m_tokenUsagePeriod == QStringLiteral("month"))
        rangeLabel = fromDate.toString(QStringLiteral("yyyy年M月"));
    else if (m_tokenUsagePeriod == QStringLiteral("total"))
        rangeLabel = tr("全部时间");
    else
        rangeLabel = tr("%1 - %2").arg(fromDate.toString(Qt::ISODate), toDate.toString(Qt::ISODate));

    const qint64 total = summary.input + summary.output + summary.cacheRead + summary.cacheWrite;
    m_tokenUsageView = {
        {QStringLiteral("period"), m_tokenUsagePeriod},
        {QStringLiteral("profile"), m_tokenUsageProfile},
        {QStringLiteral("rangeLabel"), rangeLabel},
        {QStringLiteral("from"), fromDate.toString(Qt::ISODate)},
        {QStringLiteral("to"), toDate.toString(Qt::ISODate)},
        {QStringLiteral("loading"), m_scanWatcher.isRunning() || m_usageScanWatcher.isRunning()},
        {QStringLiteral("error"), m_tokenUsageError},
        {QStringLiteral("hasData"), total > 0 || summary.cost > 0.0},
        {QStringLiteral("sessionCount"), activeSessions},
        {QStringLiteral("allSessionCount"), m_usageCache.size()},
        {QStringLiteral("eventCount"), summary.eventCount},
        {QStringLiteral("canMovePrevious"), m_tokenUsagePeriod != QStringLiteral("total")
                                                   && m_tokenUsagePeriod != QStringLiteral("custom")},
        {QStringLiteral("canMoveNext"), (m_tokenUsagePeriod == QStringLiteral("day")
                                         || m_tokenUsagePeriod == QStringLiteral("week")
                                         || m_tokenUsagePeriod == QStringLiteral("month"))
                                            && m_tokenUsageOffset < 0},
        {QStringLiteral("updatedAt"), m_tokenUsageUpdatedAt.isValid()
                                                 ? m_tokenUsageUpdatedAt.toString(QStringLiteral("HH:mm")) : QString()},
        {QStringLiteral("tokens"), QVariantMap {
             {QStringLiteral("input"), summary.input},
             {QStringLiteral("output"), summary.output},
             {QStringLiteral("cacheRead"), summary.cacheRead},
             {QStringLiteral("cacheWrite"), summary.cacheWrite},
             {QStringLiteral("total"), total}
         }},
        {QStringLiteral("cost"), summary.cost},
        {QStringLiteral("points"), points}
    };
    emit tokenUsageViewChanged();
}
