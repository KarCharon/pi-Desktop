#include "ChatModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

/**
 * 初始化聊天列表模型。
 */
ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_streamUpdateTimer.setSingleShot(true);
    m_streamUpdateTimer.setInterval(33);
    connect(&m_streamUpdateTimer, &QTimer::timeout, this, &ChatModel::flushAssistantUpdate);

    m_toolUpdateTimer.setSingleShot(true);
    m_toolUpdateTimer.setInterval(50);
    connect(&m_toolUpdateTimer, &QTimer::timeout, this, &ChatModel::flushToolUpdates);
}

/**
 * 顶层列表模型不为子索引返回行。
 */
int ChatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

/**
 * 将 ChatItem 字段映射到 QML 数据角色。
 */
QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const ChatItem &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case EntryTypeRole: return item.entryType;
    case MessageRole: return item.role;
    case ContentRole: return item.content;
    case StateRole: return item.state;
    case ToolNameRole: return item.toolName;
    case ToolInputRole: return item.toolInput;
    case ToolOutputRole: return item.toolOutput;
    case ErrorRole: return item.error;
    case TimestampRole: return item.timestamp.toString(QStringLiteral("HH:mm"));
    default: return {};
    }
}

/**
 * 声明稳定的 QML 模型角色名。
 */
QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        {IdRole, "entryId"},
        {EntryTypeRole, "entryType"},
        {MessageRole, "messageRole"},
        {ContentRole, "content"},
        {StateRole, "itemState"},
        {ToolNameRole, "toolName"},
        {ToolInputRole, "toolInput"},
        {ToolOutputRole, "toolOutput"},
        {ErrorRole, "isError"},
        {TimestampRole, "timestamp"}
    };
}

/**
 * 重置模型并清除当前流式消息游标。
 */
void ChatModel::clear()
{
    m_streamUpdateTimer.stop();
    m_toolUpdateTimer.stop();
    m_pendingAssistantRow = -1;
    m_pendingToolRows.clear();
    beginResetModel();
    m_items.clear();
    m_streamingAssistantId.clear();
    endResetModel();
    emit contentUpdated();
}

/**
 * 添加一条已完成的本地用户消息。
 */
QString ChatModel::appendUserMessage(const QString &content)
{
    ChatItem item;
    item.id = QStringLiteral("local-user-%1").arg(m_nextLocalId++);
    item.entryType = QStringLiteral("message");
    item.role = QStringLiteral("user");
    item.content = content;
    item.state = QStringLiteral("completed");
    item.timestamp = QDateTime::currentDateTime();
    const QString id = item.id;
    appendItem(std::move(item));
    return id;
}

/**
 * 复用当前助手消息，或创建一条新的流式助手消息。
 */
QString ChatModel::ensureStreamingAssistant()
{
    if (!m_streamingAssistantId.isEmpty()) {
        return m_streamingAssistantId;
    }

    ChatItem item;
    item.id = QStringLiteral("local-assistant-%1").arg(m_nextLocalId++);
    item.entryType = QStringLiteral("message");
    item.role = QStringLiteral("assistant");
    item.state = QStringLiteral("streaming");
    item.timestamp = QDateTime::currentDateTime();
    m_streamingAssistantId = item.id;
    appendItem(std::move(item));
    return m_streamingAssistantId;
}

/**
 * 仅修改当前助手条目的文本角色，避免流式期间重建 delegate。
 */
void ChatModel::appendAssistantDelta(const QString &delta)
{
    ensureStreamingAssistant();
    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (m_items.at(row).id == m_streamingAssistantId) {
            m_items[row].content.append(delta);
            m_pendingAssistantRow = row;
            if (!m_streamUpdateTimer.isActive()) {
                m_streamUpdateTimer.start();
            }
            return;
        }
    }
}

/**
 * 用完整结束消息校正流式拼接结果和错误状态。
 */
void ChatModel::finishAssistant(const QString &content, bool failed)
{
    flushAssistantUpdate();
    flushToolUpdates();
    if (m_streamingAssistantId.isEmpty() && content.isEmpty() && !failed) {
        return;
    }
    ensureStreamingAssistant();
    for (int row = m_items.size() - 1; row >= 0; --row) {
        ChatItem &item = m_items[row];
        if (item.id == m_streamingAssistantId) {
            if (content.isEmpty() && item.content.isEmpty() && !failed) {
                // 纯 Tool Call 的 assistant 消息不显示空气泡，工具卡片由独立条目承载。
                beginRemoveRows({}, row, row);
                m_items.removeAt(row);
                endRemoveRows();
                m_streamingAssistantId.clear();
                emit contentUpdated();
                return;
            }
            if (!content.isEmpty()) {
                item.content = content;
            }
            if (failed && item.content.isEmpty()) {
                item.content = tr("Pi 请求失败，未返回错误详情。");
            }
            item.error = failed;
            item.state = failed ? QStringLiteral("failed") : QStringLiteral("completed");
            const QModelIndex changedIndex = index(row);
            emit dataChanged(changedIndex, changedIndex, {ContentRole, StateRole, ErrorRole});
            m_streamingAssistantId.clear();
            emit contentUpdated();
            return;
        }
    }
}

/**
 * 添加工具卡片，重复的开始事件只更新原卡片。
 */
void ChatModel::startTool(const QString &id, const QString &name, const QString &input)
{
    const int existing = findTool(id);
    if (existing >= 0) {
        ChatItem &item = m_items[existing];
        item.toolName = name;
        item.toolInput = input;
        item.state = QStringLiteral("running");
        const QModelIndex changedIndex = index(existing);
        emit dataChanged(changedIndex, changedIndex, {ToolNameRole, ToolInputRole, StateRole});
        emit contentUpdated();
        return;
    }

    ChatItem item;
    item.id = id.isEmpty() ? QStringLiteral("local-tool-%1").arg(m_nextLocalId++) : id;
    item.entryType = QStringLiteral("tool");
    item.role = QStringLiteral("tool");
    item.state = QStringLiteral("running");
    item.toolName = name;
    item.toolInput = input;
    item.timestamp = QDateTime::currentDateTime();
    appendItem(std::move(item));
}

/**
 * 替换工具执行事件提供的累计输出。
 */
void ChatModel::updateTool(const QString &id, const QString &output)
{
    const int row = findTool(id);
    if (row < 0) {
        return;
    }
    m_items[row].toolOutput = output;
    m_pendingToolRows.insert(row);
    if (!m_toolUpdateTimer.isActive()) {
        m_toolUpdateTimer.start();
    }
}

/**
 * 保存最终工具输出并标记完成或失败。
 */
void ChatModel::finishTool(const QString &id, const QString &output, bool failed)
{
    int row = findTool(id);
    if (row < 0) {
        startTool(id, tr("未知工具"), {});
        row = findTool(id);
    }
    if (row < 0) {
        return;
    }

    m_pendingToolRows.remove(row);
    ChatItem &item = m_items[row];
    item.toolOutput = output;
    item.error = failed;
    item.state = failed ? QStringLiteral("failed") : QStringLiteral("completed");
    const QModelIndex changedIndex = index(row);
    emit dataChanged(changedIndex, changedIndex, {ToolOutputRole, ErrorRole, StateRole});
    emit contentUpdated();
}

/**
 * 重建历史消息，并把 assistant 内的 toolCall 与后续 toolResult 合并成卡片。
 */
void ChatModel::replaceFromMessages(const QJsonArray &messages)
{
    m_streamUpdateTimer.stop();
    m_toolUpdateTimer.stop();
    m_pendingAssistantRow = -1;
    m_pendingToolRows.clear();
    beginResetModel();
    m_items.clear();
    m_streamingAssistantId.clear();

    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(message.value(QStringLiteral("timestamp")).toDouble()));

        if (role == QStringLiteral("user") || role == QStringLiteral("assistant")) {
            QString text = extractText(message.value(QStringLiteral("content")));
            const bool failed = message.value(QStringLiteral("stopReason")).toString() == QStringLiteral("error");
            const QString detail = message.value(QStringLiteral("errorMessage")).toString();
            if (failed) {
                text += (text.isEmpty() ? QString() : QStringLiteral("\n\n"))
                        + (detail.isEmpty() ? tr("Pi 请求失败，未返回错误详情。") : detail);
            }
            if (!text.isEmpty()) {
                ChatItem item;
                item.id = QStringLiteral("history-%1").arg(m_nextLocalId++);
                item.entryType = QStringLiteral("message");
                item.role = role;
                item.content = text;
                item.state = failed ? QStringLiteral("failed") : QStringLiteral("completed");
                item.error = failed;
                item.timestamp = timestamp;
                m_items.append(std::move(item));
            }

            if (role == QStringLiteral("assistant") && message.value(QStringLiteral("content")).isArray()) {
                const QJsonArray blocks = message.value(QStringLiteral("content")).toArray();
                for (const QJsonValue &blockValue : blocks) {
                    const QJsonObject block = blockValue.toObject();
                    if (block.value(QStringLiteral("type")).toString() != QStringLiteral("toolCall")) {
                        continue;
                    }
                    ChatItem tool;
                    tool.id = block.value(QStringLiteral("id")).toString();
                    tool.entryType = QStringLiteral("tool");
                    tool.role = QStringLiteral("tool");
                    tool.toolName = block.value(QStringLiteral("name")).toString();
                    tool.toolInput = QString::fromUtf8(QJsonDocument(block.value(QStringLiteral("arguments")).toObject())
                                                          .toJson(QJsonDocument::Indented));
                    tool.state = QStringLiteral("completed");
                    tool.timestamp = timestamp;
                    m_items.append(std::move(tool));
                }
            }
        } else if (role == QStringLiteral("toolResult")) {
            const QString toolCallId = message.value(QStringLiteral("toolCallId")).toString();
            int toolRow = -1;
            for (int row = m_items.size() - 1; row >= 0; --row) {
                if (m_items.at(row).entryType == QStringLiteral("tool") && m_items.at(row).id == toolCallId) {
                    toolRow = row;
                    break;
                }
            }
            if (toolRow < 0) {
                ChatItem tool;
                tool.id = toolCallId;
                tool.entryType = QStringLiteral("tool");
                tool.role = QStringLiteral("tool");
                tool.toolName = message.value(QStringLiteral("toolName")).toString();
                tool.timestamp = timestamp;
                m_items.append(std::move(tool));
                toolRow = m_items.size() - 1;
            }
            ChatItem &tool = m_items[toolRow];
            tool.toolOutput = extractText(message.value(QStringLiteral("content")));
            tool.error = message.value(QStringLiteral("isError")).toBool();
            tool.state = tool.error ? QStringLiteral("failed") : QStringLiteral("completed");
        }
    }

    endResetModel();
    emit contentUpdated();
}

/**
 * 显示连接、协议和运行错误，不让故障只停留在日志中。
 */
void ChatModel::appendSystemMessage(const QString &content, bool failed)
{
    ChatItem item;
    item.id = QStringLiteral("system-%1").arg(m_nextLocalId++);
    item.entryType = QStringLiteral("message");
    item.role = QStringLiteral("system");
    item.content = content;
    item.state = failed ? QStringLiteral("failed") : QStringLiteral("completed");
    item.error = failed;
    item.timestamp = QDateTime::currentDateTime();
    appendItem(std::move(item));
}

/**
 * 将标准错误更新到同一诊断卡片；内容由控制器限长并批量刷新。
 */
void ChatModel::updateProcessDiagnostic(const QString &content)
{
    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (m_items.at(row).id == QStringLiteral("pi-stderr")) {
            m_items[row].content = content;
            emit dataChanged(index(row), index(row), {ContentRole});
            emit contentUpdated();
            return;
        }
    }
    ChatItem item;
    item.id = QStringLiteral("pi-stderr");
    item.entryType = QStringLiteral("message");
    item.role = QStringLiteral("system");
    item.content = content;
    item.state = QStringLiteral("completed");
    item.timestamp = QDateTime::currentDateTime();
    appendItem(std::move(item));
}

/**
 * 将 33 毫秒内收到的文本增量合并成一次 dataChanged 与自动滚动通知。
 */
void ChatModel::flushAssistantUpdate()
{
    m_streamUpdateTimer.stop();
    if (m_pendingAssistantRow < 0 || m_pendingAssistantRow >= m_items.size()) {
        m_pendingAssistantRow = -1;
        return;
    }

    const QModelIndex changedIndex = index(m_pendingAssistantRow);
    emit dataChanged(changedIndex, changedIndex, {ContentRole});
    m_pendingAssistantRow = -1;
    emit contentUpdated();
}

/**
 * 每 50 毫秒批量通知累计 Tool Output，降低高频工具回调的布局压力。
 */
void ChatModel::flushToolUpdates()
{
    m_toolUpdateTimer.stop();
    bool changed = false;
    for (const int row : std::as_const(m_pendingToolRows)) {
        if (row < 0 || row >= m_items.size()) {
            continue;
        }
        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {ToolOutputRole});
        changed = true;
    }
    m_pendingToolRows.clear();
    if (changed) {
        emit contentUpdated();
    }
}

/**
 * 兼容字符串内容和 TextContent 数组，忽略图片与思考块。
 */
QString ChatModel::extractText(const QJsonValue &content)
{
    if (content.isString()) {
        return content.toString();
    }
    if (!content.isArray()) {
        return {};
    }

    QStringList parts;
    for (const QJsonValue &value : content.toArray()) {
        const QJsonObject block = value.toObject();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            parts.append(block.value(QStringLiteral("text")).toString());
        }
    }
    return parts.join(QStringLiteral("\n\n"));
}

/**
 * 提取工具事件 result.content 中的文本块。
 */
QString ChatModel::extractToolOutput(const QJsonObject &result)
{
    return extractText(result.value(QStringLiteral("content")));
}

/**
 * 线性查找工具卡片；单会话工具数量较少，避免额外索引同步成本。
 */
int ChatModel::findTool(const QString &id) const
{
    for (int row = m_items.size() - 1; row >= 0; --row) {
        const ChatItem &item = m_items.at(row);
        if (item.entryType == QStringLiteral("tool") && item.id == id) {
            return row;
        }
    }
    return -1;
}

/**
 * 以标准 begin/endInsertRows 顺序追加条目。
 */
void ChatModel::appendItem(ChatItem item)
{
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    m_items.append(std::move(item));
    endInsertRows();
    emit contentUpdated();
}
