#pragma once

#include <QJsonObject>
#include <QString>
#include <QMetaType>

/**
 * 表示 Pi RPC 输出的一条结构化事件。
 */
class PiEvent
{
public:
    /**
     * 使用原始 JSON 对象创建事件。
     */
    explicit PiEvent(QJsonObject payload = {});

    /**
     * 返回事件类型。
     */
    [[nodiscard]] QString type() const;

    /**
     * 返回事件的原始 JSON 数据。
     */
    [[nodiscard]] const QJsonObject &payload() const;

private:
    QJsonObject m_payload;
};

Q_DECLARE_METATYPE(PiEvent)
