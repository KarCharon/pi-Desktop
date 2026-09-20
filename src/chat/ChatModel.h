#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QVector>

/**
 * 表示聊天列表中的消息或工具调用卡片。
 */
struct ChatItem
{
    QString id;
    QString entryType;
    QString role;
    QString content;
    QString state;
    QString toolName;
    QString toolInput;
    QString toolOutput;
    bool error = false;
    QDateTime timestamp;
};

/**
 * 向 QML 提供可虚拟化的聊天消息与 Tool Call 列表模型。
 */
class ChatModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    /** QML 可访问的数据角色。 */
    enum Role {
        IdRole = Qt::UserRole + 1,
        EntryTypeRole,
        MessageRole,
        ContentRole,
        StateRole,
        ToolNameRole,
        ToolInputRole,
        ToolOutputRole,
        ErrorRole,
        TimestampRole
    };
    Q_ENUM(Role)

    /**
     * 创建空聊天模型。
     */
    explicit ChatModel(QObject *parent = nullptr);

    /**
     * 返回模型行数。
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * 返回指定行和角色的数据。
     */
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    /**
     * 返回 QML 角色名映射。
     */
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /**
     * 清空全部聊天条目。
     */
    Q_INVOKABLE void clear();

    /**
     * 追加用户消息。
     */
    QString appendUserMessage(const QString &content);

    /**
     * 确保存在一条正在流式生成的助手消息。
     */
    QString ensureStreamingAssistant();

    /**
     * 向当前流式助手消息追加文本。
     */
    void appendAssistantDelta(const QString &delta);

    /**
     * 使用 message_end 的权威内容结束助手消息。
     */
    void finishAssistant(const QString &content, bool failed);

    /**
     * 创建或更新一张工具调用卡片。
     */
    void startTool(const QString &id, const QString &name, const QString &input);

    /**
     * 用累计工具输出刷新卡片。
     */
    void updateTool(const QString &id, const QString &output);

    /**
     * 完成工具调用并设置最终状态。
     */
    void finishTool(const QString &id, const QString &output, bool failed);

    /**
     * 将 get_messages 返回的 Pi 消息转换为列表条目。
     */
    void replaceFromMessages(const QJsonArray &messages);

    /**
     * 追加一条系统诊断消息。
     */
    void appendSystemMessage(const QString &content, bool failed = false);

    /** 更新单张有界的标准错误诊断卡片，避免连续 stderr 产生大量消息。 */
    void updateProcessDiagnostic(const QString &content);

signals:
    /** 消息内容更新，供聊天视图跟随滚动。 */
    void contentUpdated();

private:
    /**
     * 合并一批流式增量并一次通知 QML，降低 Markdown 重排频率。
     */
    void flushAssistantUpdate();

    /**
     * 合并工具流式输出更新，避免高频刷新折叠卡片。
     */
    void flushToolUpdates();

    /**
     * 从 Pi 内容字段提取可显示文本。
     */
    [[nodiscard]] static QString extractText(const QJsonValue &content);

    /**
     * 从工具结果内容数组提取文本。
     */
    [[nodiscard]] static QString extractToolOutput(const QJsonObject &result);

    /**
     * 按工具调用 ID 查找条目索引。
     */
    [[nodiscard]] int findTool(const QString &id) const;

    /**
     * 追加一个模型条目并通知视图。
     */
    void appendItem(ChatItem item);

    QVector<ChatItem> m_items;
    QString m_streamingAssistantId;
    quint64 m_nextLocalId = 1;
    QTimer m_streamUpdateTimer;
    QTimer m_toolUpdateTimer;
    int m_pendingAssistantRow = -1;
    QSet<int> m_pendingToolRows;
};
