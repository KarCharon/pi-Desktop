#include "PiEvent.h"

#include <utility>

/**
 * 保存 Pi RPC 事件载荷。
 */
PiEvent::PiEvent(QJsonObject payload)
    : m_payload(std::move(payload))
{
}

/**
 * 读取 RPC 事件的 type 字段。
 */
QString PiEvent::type() const
{
    return m_payload.value(QStringLiteral("type")).toString();
}

/**
 * 提供只读原始载荷，供上层按事件类型解析。
 */
const QJsonObject &PiEvent::payload() const
{
    return m_payload;
}
