import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 无容器气泡的文档式 Assistant 消息，突出 Markdown 内容本身。
 */
Item {
    id: root
    required property string messageText
    required property string timeText
    required property bool streaming
    required property bool failed
    property string workingText: "Thinking"

    implicitHeight: contentColumn.implicitHeight + 2

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 15
                color: "#e7d8bd"
                Label {
                    anchors.centerIn: parent
                    text: "Pi"
                    color: "#543d2d"
                    font.family: "serif"
                    font.bold: true
                    font.pixelSize: 13
                }
            }
            Label {
                text: root.failed ? "Pi · Error" : "Pi"
                color: root.failed ? "#b3554d" : "#4c4943"
                font.pixelSize: 13
                font.bold: true
            }
            WorkingIndicator {
                text: root.streaming ? root.workingText : "·  " + root.timeText
                running: root.streaming
                color: "#9b958b"
                fontPixelSize: 11
            }
        }

        TextEdit {
            Layout.fillWidth: true
            text: root.messageText.length > 0 ? root.messageText : "…"
            // 流式阶段避免每批增量重解析整段 Markdown，结束后再渲染格式。
            textFormat: root.streaming ? TextEdit.PlainText : TextEdit.MarkdownText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: root.failed ? "#8f4844" : "#383631"
            font.pixelSize: 15
            font.letterSpacing: 0
            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }
}
