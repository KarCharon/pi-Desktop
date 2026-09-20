import QtQuick
import QtQuick.Controls

/**
 * 居中展示连接、协议与扩展通知。
 */
Rectangle {
    id: root
    required property string messageText
    required property bool failed

    implicitHeight: systemLabel.implicitHeight + 18
    radius: 8
    color: root.failed ? "#301c22" : "#161c26"
    border.color: root.failed ? "#74404a" : "#283143"

    Label {
        id: systemLabel
        anchors.fill: parent
        anchors.margins: 9
        text: root.messageText
        color: root.failed ? "#ff9aa6" : "#8793a7"
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: 12
    }
}
