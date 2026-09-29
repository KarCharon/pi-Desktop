#include "PetStateAdapter.h"

#include "agent/AgentSessionController.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(petStateLog, "pidesktop.pet.state")

/** 创建适配器并立即订阅 Agent 的结构化状态通知。 */
PetStateAdapter::PetStateAdapter(AgentSessionController *agent, QObject *parent)
    : QObject(parent)
    , m_agent(agent)
{
    Q_ASSERT(m_agent);
    connect(m_agent, &AgentSessionController::petStateChanged,
            this, &PetStateAdapter::updateFromAgent);
    m_snapshot = {
        {QStringLiteral("state"), QStringLiteral("disconnected")},
        {QStringLiteral("sessionId"), QString()},
        {QStringLiteral("generation"), QVariant::fromValue<qulonglong>(0)},
        {QStringLiteral("sequence"), QVariant::fromValue<qulonglong>(0)},
        {QStringLiteral("changedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };
}

/** 返回桌宠消费的不可变状态快照副本。 */
QVariantMap PetStateAdapter::snapshot() const
{
    return m_snapshot;
}

/** 丢弃旧会话、旧代次和倒退序号，避免异步事件覆盖当前状态。 */
void PetStateAdapter::updateFromAgent(const QVariantMap &state)
{
    const quint64 generation = state.value(QStringLiteral("generation")).toULongLong();
    const quint64 sequence = state.value(QStringLiteral("sequence")).toULongLong();
    const QString sessionId = state.value(QStringLiteral("sessionId")).toString();
    const QString currentSession = m_snapshot.value(QStringLiteral("sessionId")).toString();
    if (generation < m_generation || (generation == m_generation && sequence <= m_sequence)) {
        qCInfo(petStateLog) << "[PetState] 丢弃旧状态；generation=" << generation
                            << "sequence=" << sequence;
        return;
    }
    if (generation == m_generation && !currentSession.isEmpty()
        && !sessionId.isEmpty() && sessionId != currentSession
        && state.value(QStringLiteral("state")).toString() != QStringLiteral("disconnected")) {
        qCInfo(petStateLog) << "[PetState] 丢弃旧会话状态；reason=session-mismatch";
        return;
    }
    m_generation = generation;
    m_sequence = sequence;
    m_snapshot = state;
    if (!m_snapshot.contains(QStringLiteral("changedAt")))
        m_snapshot.insert(QStringLiteral("changedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    emit snapshotChanged();
    qCInfo(petStateLog) << "[PetState] 状态更新；state="
                        << m_snapshot.value(QStringLiteral("state")).toString()
                        << "generation=" << generation << "sequence=" << sequence;
}
