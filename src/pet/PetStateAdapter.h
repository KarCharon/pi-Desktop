#pragma once

#include <QObject>
#include <QVariantMap>

class AgentSessionController;

/**
 * 将 Agent 的结构化运行状态转换为桌宠使用的稳定快照。
 */
class PetStateAdapter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)

public:
    /** 创建状态适配器并绑定当前 Agent。 */
    explicit PetStateAdapter(AgentSessionController *agent, QObject *parent = nullptr);

    /** 返回当前状态快照，包含状态、会话、代次和变化时间。 */
    QVariantMap snapshot() const;

signals:
    /** 状态发生有意义变化时通知桌宠控制器。 */
    void snapshotChanged();

private:
    /** 接收 Agent 状态信号并过滤旧会话或旧代次事件。 */
    void updateFromAgent(const QVariantMap &state);

    AgentSessionController *m_agent; ///< 当前 Pi Agent，生命周期由 AppController 管理。
    QVariantMap m_snapshot; ///< 最近一次通过顺序校验的桌宠状态快照。
    quint64 m_generation = 0; ///< 已接受的 Agent 运行代次。
    quint64 m_sequence = 0; ///< 已接受的状态变化序号。
};
