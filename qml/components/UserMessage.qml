import QtQuick

/**
 * 以轻米色背景展示用户消息，不使用强烈聊天气泡或头像。
 */
Item {
    id: root
    required property string messageText
    required property string timeText
    implicitHeight: bubble.height + Theme.scaled(4)

    Rectangle {
        id: bubble
        width: Math.min(parent.width, Math.max(Theme.scaled(260), userText.implicitWidth + Theme.scaled(34)))
        height: userText.implicitHeight + Theme.scaled(28)
        anchors.right: parent.right
        radius: Theme.scaled(10)
        color: Theme.userBubble

        TextEdit {
            id: userText
            anchors.fill: parent
            anchors.margins: Theme.scaled(14)
            text: root.messageText
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: Theme.textPrimary
            font.pixelSize: Theme.scaled(14)
            // 使用平台原生渲染，避免选中/重绘时距离场渲染造成字重忽粗忽细。
            renderType: TextEdit.NativeRendering
        }
    }
}
