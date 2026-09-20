#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonValue>
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

signals:
    /** 会话数量发生变化。 */
    void countChanged();
    /** 后台扫描状态发生变化。 */
    void loadingChanged();
    /** 会话扫描完成，携带有效和无效文件统计。 */
    void refreshCompleted(int validCount, int invalidCount);

private:
    /**
     * 保存已解析文件的时间戳、大小和 Session 元数据。
     */
    struct CacheEntry
    {
        qint64 size = 0;
        QDateTime modifiedAt;
        SessionInfo session;
    };

    /**
     * 保存一次后台扫描的完整结果和下一轮增量缓存。
     */
    struct ScanResult
    {
        QVector<SessionInfo> sessions;
        QHash<QString, CacheEntry> cache;
        int invalidCount = 0;
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

    QVector<SessionInfo> m_sessions;
    QString m_sessionRoot;
    QString m_scanningRoot;
    QFutureWatcher<ScanResult> m_scanWatcher;
    QHash<QString, CacheEntry> m_cache;
    bool m_refreshPending = false;
};
