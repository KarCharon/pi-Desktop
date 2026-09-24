import QtQuick
import QtQuick.Controls

/**
 * 居中展示连接、协议与扩展通知。
 */
Rectangle {
    id: root
    required property string messageText
    required property bool failed

    implicitHeight: systemLabel.implicitHeight + Theme.scaled(18)
    radius: Theme.scaled(8)
    color: root.failed ? Theme.systemFailBg : Theme.systemBg
    border.color: root.failed ? Theme.systemFailBorder : Theme.systemBorder

    Label {
        id: systemLabel
        anchors.fill: parent
        anchors.margins: Theme.scaled(9)
        text: root.messageText
        color: root.failed ? Theme.systemFailText : Theme.systemText
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.scaled(12)
    }
}
