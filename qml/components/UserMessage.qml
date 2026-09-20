import QtQuick

/**
 * 以轻米色背景展示用户消息，不使用强烈聊天气泡或头像。
 */
Item {
    id: root
    required property string messageText
    required property string timeText
    implicitHeight: bubble.height + 4

    Rectangle {
        id: bubble
        width: Math.min(parent.width, Math.max(260, userText.implicitWidth + 34))
        height: userText.implicitHeight + 28
        anchors.right: parent.right
        radius: 10
        color: "#eee9df"

        TextEdit {
            id: userText
            anchors.fill: parent
            anchors.margins: 14
            text: root.messageText
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: "#393631"
            font.pixelSize: 14
        }
    }
}
