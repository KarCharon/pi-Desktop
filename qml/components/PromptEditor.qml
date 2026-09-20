import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 参考设计稿提供文档式 Composer，支持 Enter 发送和 Shift+Enter 换行。
 */
Item {
    id: root
    property alias text: editor.text
    required property bool busy
    required property bool connected
    signal submit(string text, bool followUp)
    signal retrieveRequested()
    signal abortRequested()

    implicitHeight: Math.min(190, Math.max(94, editor.contentHeight + 42))

    Rectangle {
        anchors.fill: parent
        radius: 13
        color: "#ffffff"
        border.color: editor.activeFocus ? "#c87b59" : "#e2ddd5"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            TextArea {
                id: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: root.connected ? "Ask pi…" : "正在连接 Pi…"
                enabled: root.connected
                color: "#403d37"
                placeholderTextColor: "#aaa49b"
                wrapMode: TextEdit.Wrap
                background: null
                leftPadding: 4
                rightPadding: 4
                topPadding: 2
                bottomPadding: 0
                font.pixelSize: 14

                Keys.onPressed: event => {
                    const isEnter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    const wantsNewLine = (event.modifiers & Qt.ShiftModifier) !== 0
                    if (isEnter && !wantsNewLine) {
                        root.submitCurrent((event.modifiers & Qt.AltModifier) !== 0)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape && root.busy) {
                        root.abortRequested()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                        root.retrieveRequested()
                        event.accepted = true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    id: addButton
                    text: "+"
                    implicitWidth: 34
                    implicitHeight: 30
                    font.pixelSize: 20
                    ToolTip.visible: hovered
                    ToolTip.text: "添加文件或上下文"
                    background: Rectangle { color: addButton.down ? "#ede8e0" : "#f7f5f0"; border.color: "#e5dfd6"; radius: 8 }
                }
                Button {
                    id: contextButton
                    text: "▣  Context"
                    implicitHeight: 30
                    font.pixelSize: 12
                    contentItem: Text { text: contextButton.text; color: "#625e57"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: contextButton.down ? "#ede8e0" : "#f7f5f0"; border.color: "#e5dfd6"; radius: 8 }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.busy ? "Enter 引导 · Alt+Enter 后续 · Esc 停止" : "Enter 发送 · Shift+Enter 换行"
                    color: "#9b958b"
                    font.pixelSize: 10
                }
                Button {
                    visible: root.busy
                    text: "停止"
                    onClicked: root.abortRequested()
                }
                Button {
                    id: sendButton
                    text: root.busy ? "引导" : "发送"
                    implicitWidth: 76
                    implicitHeight: 34
                    enabled: root.connected && editor.text.trim().length > 0
                    font.pixelSize: 12
                    font.bold: true
                    palette.buttonText: "#ffffff"
                    background: Rectangle { color: sendButton.enabled ? (sendButton.down ? "#ad6248" : "#c57958") : "#d9b9aa"; radius: 9 }
                    onClicked: root.submitCurrent(false)
                }
            }
        }
    }

    /**
     * 去除纯空白输入并发出提交信号。
     */
    function submitCurrent(followUp = false) {
        let value = editor.text.trim()
        if (value.length > 0 && root.connected)
            root.submit(value, followUp)
    }
}
