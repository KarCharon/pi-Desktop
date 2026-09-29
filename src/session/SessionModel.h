#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>
#include <QVector>

/**
 * 描述一个由 Pi Session JSONL 文件提供的会话。
 */
struct SessionInfo
{
    QString id;
    QString name;
    QString path;
    QString projectPath;
    QString preview;
    QDateTime createdAt;
    QDateTime updatedAt;
};

/**
 * 扫描并展示 Pi 的历史 Session；模型不保存 Agent 真实状态。
 */
class SessionModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantMap tokenUsageView READ tokenUsageView NOTIFY tokenUsageViewChanged)
    Q_PROPERTY(QString tokenUsageProfile READ tokenUsageProfile NOTIFY tokenUsageProfileChanged)
    Q_PROPERTY(QStringList tokenUsageProfiles READ tokenUsageProfiles NOTIFY tokenUsageProfilesChanged)

public:
    /** QML 可访问的 Session 字段。 */
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        ProjectPathRole,
        PreviewRole,
        CreatedAtRole,
        UpdatedAtRole,
        SessionFolderRole
    };
    Q_ENUM(Role)

    /**
     * 创建历史会话列表。
     */
    explicit SessionModel(QObject *parent = nullptr);

    /**
     * 返回 Session 数量。
     */
    [[nodiscard]] int count() const;

    /**
     * 返回后台 Session 扫描是否正在运行。
     */
    [[nodiscard]] bool loading() const;

    /**
     * 返回列表行数。
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * 返回指定 Session 的角色数据。
     */
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    /**
     * 返回 QML 角色名映射。
     */
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /**
     * 重新扫描 Pi 默认 Session 目录。
     */
    Q_INVOKABLE void refresh();

    /**
     * 配置要扫描的 Pi Session 根目录。
     */
    void setSessionRoot(const QString &sessionRoot);

    /**
     * 返回当前时间范围内的全局 Token 汇总、趋势点和会话数量。
     */
    [[nodiscard]] QVariantMap tokenUsageView() const;

    /** 返回当前用于 Token 统计的 Profile 目录。 */
    [[nodiscard]] QString tokenUsageProfile() const;

    /** 返回可供 Token 统计筛选的 Profile 目录。 */
    [[nodiscard]] QStringList tokenUsageProfiles() const;

    /** 更新可供 Token 统计筛选的 Profile 目录列表。 */
    void setTokenUsageProfiles(const QStringList &profiles);

    /** 切换 Token 统计使用的 Profile，不改变当前活动会话。 */
    Q_INVOKABLE bool setTokenUsageProfile(const QString &profile);

    /**
     * 切换 Token 统计周期；支持 day、week、month、total 和 custom。
     */
    Q_INVOKABLE void setTokenUsagePeriod(const QString &period);

    /**
     * 将日、周或月统计范围前后移动指定周期数。
     */
    Q_INVOKABLE void shiftTokenUsagePeriod(int amount);

    /**
     * 应用包含首尾日期的自定义 Token 统计范围。
     */
    Q_INVOKABLE bool setCustomTokenUsageRange(const QString &from, const QString &to);

signals:
    /** 会话数量发生变化。 */
    void countChanged();
    /** 后台扫描状态发生变化。 */
    void loadingChanged();
    /** 会话扫描完成，携带有效和无效文件统计。 */
    void refreshCompleted(int validCount, int invalidCount);
    /** 全局 Token 的数据、周期或加载状态发生变化。 */
    void tokenUsageViewChanged();
    /** Token 统计的 Profile 发生变化。 */
    void tokenUsageProfileChanged();
    /** Token 统计可选 Profile 列表发生变化。 */
    void tokenUsageProfilesChanged();

private:
    /**
     * 保存一个时间桶内的 Token、费用与事件总量。
     */
    struct UsageTotals
    {
        qint64 input = 0; ///< 未命中缓存的输入 Token。
        qint64 output = 0; ///< 模型输出 Token。
        qint64 cacheRead = 0; ///< 缓存读取 Token。
        qint64 cacheWrite = 0; ///< 缓存写入 Token。
        double cost = 0.0; ///< Pi 记录的美元费用。
        int eventCount = 0; ///< 纳入统计的 usage 事件数量。
    };

    /**
     * 保存已解析文件的时间戳、大小、Session 元数据和小时统计缓存。
     */
    struct CacheEntry
    {
        qint64 size = 0; ///< 上次扫描时的文件大小。
        QDateTime modifiedAt; ///< 上次扫描时的文件修改时间。
        SessionInfo session; ///< 供会话导航展示的元数据。
        qint64 parsedUsageBytes = 0; ///< 已处理到的最后一个完整 JSONL 换行位置。
        QHash<QString, UsageTotals> usageByHour; ///< 本地时间小时键到 usage 汇总的映射。
        int skippedUsageCount = 0; ///< 时间戳或 usage 无效而跳过的记录数。
    };

    /**
     * 保存一次后台扫描的完整结果和下一轮增量缓存。
     */
    struct ScanResult
    {
        QVector<SessionInfo> sessions; ///< 扫描得到的有效会话。
        QHash<QString, CacheEntry> cache; ///< 可复用于下一轮的文件缓存。
        int invalidCount = 0; ///< 无法解析元数据的文件数。
        int usageFileCount = 0; ///< 至少包含一个有效 usage 的文件数。
        int usageEventCount = 0; ///< 全部缓存中的有效 usage 事件数。
        int skippedUsageCount = 0; ///< 全部缓存中的无效 usage 记录数。
        qint64 totalTokens = 0; ///< 全部缓存中的 Token 总量。
    };

    /**
     * 在工作线程中增量扫描指定 Session 根目录。
     */
    [[nodiscard]] static ScanResult scanSessions(const QString &root,
                                                 const QHash<QString, CacheEntry> &previousCache);

    /**
     * 解析单个 JSONL 文件中的 Session 元数据。
     */
    [[nodiscard]] static bool parseSessionFile(const QString &path, SessionInfo &session);

    /**
     * 从消息内容中提取预览文本。
     */
    [[nodiscard]] static QString extractPreview(const QJsonValue &content);

    /**
     * 增量解析一个 Session 文件中的 assistant、工具摘要和压缩 usage。
     */
    static void parseSessionUsage(const QString &path, const CacheEntry *previous,
                                  CacheEntry &entry);

    /**
     * 将一个 usage 对象累加到对应的本地小时桶。
     */
    static bool addUsageRecord(const QJsonObject &usage, const QDateTime &timestamp,
                               QHash<QString, UsageTotals> &buckets);

    /**
     * 根据当前周期和缓存重建供 QML 使用的汇总视图。
     */
    void rebuildTokenUsageView();

    /** 启动非当前活动 Profile 的 Token JSONL 后台扫描。 */
    void startTokenUsageScan();

    QVector<SessionInfo> m_sessions; ///< 当前 Profile 的会话元数据。
    QString m_sessionRoot; ///< 当前 Profile 的 sessions 根目录。
    QString m_scanningRoot; ///< 后台任务实际扫描的根目录。
    QFutureWatcher<ScanResult> m_scanWatcher; ///< 当前活动 Profile 的非阻塞扫描任务观察器。
    QFutureWatcher<ScanResult> m_usageScanWatcher; ///< 统计筛选 Profile 的非阻塞扫描任务观察器。
    QHash<QString, CacheEntry> m_cache; ///< 当前活动 Profile 按文件路径保存的增量解析缓存。
    QHash<QString, CacheEntry> m_usageCache; ///< 统计筛选 Profile 按文件路径保存的增量解析缓存。
    bool m_refreshPending = false; ///< 当前活动 Profile 扫描期间是否收到过合并刷新请求。
    bool m_usageRefreshPending = false; ///< 统计 Profile 扫描期间是否收到过合并刷新请求。
    QVariantMap m_tokenUsageView; ///< 已按当前周期聚合的 QML 数据快照。
    QString m_tokenUsagePeriod = QStringLiteral("month"); ///< 当前统计周期。
    int m_tokenUsageOffset = 0; ///< 相对当前日、周或月的偏移量。
    QStringList m_tokenUsageProfiles; ///< 可供侧栏筛选的 Profile 目录。
    QString m_tokenUsageProfile; ///< 当前选中的 Token 统计 Profile 目录。
    QString m_usageProfileRoot; ///< 当前选中 Profile 的 sessions 目录。
    QString m_usageScanningRoot; ///< Token 后台任务实际扫描的目录。
    QDate m_customUsageFrom; ///< 自定义范围首日，包含当天。
    QDate m_customUsageTo; ///< 自定义范围末日，包含当天。
    QString m_tokenUsageError; ///< 最近一次时间范围校验错误。
    QDateTime m_tokenUsageUpdatedAt; ///< 最近一次成功扫描完成时间。
};
