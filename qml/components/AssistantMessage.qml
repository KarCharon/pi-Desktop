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
    /** 瞬时清空显示文本的标记；确保缩放时从原始消息重新导入 Markdown。 */
    property bool markdownReparsePass: false

    implicitHeight: contentColumn.implicitHeight + Theme.scaled(2)

    // Qt 的 Markdown 解析器在导入时把代码文字的字体尺寸固化下来，
    // 只改 font.pixelSize 不会重新解析，导致代码块/行内代码不随缩放变小。
    /**
     * 强制重新导入 Markdown，使代码文字的字号跟随当前缩放。
     *
     * 同一帧内清空并恢复原始消息，不切换格式，避免 Qt 回读并重复转义 Markdown。
     * 在实际字号更新后调用，保证代码字体使用新字号；流式纯文本无需重解析。
     */
    function forceMarkdownReparse() {
        if (root.streaming || root.messageText.length === 0)
            return
        markdownReparsePass = true
        markdownReparsePass = false
    }

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.scaled(9)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(15)
                color: Theme.avatarBg
                Label {
                    anchors.centerIn: parent
                    text: "Pi"
                    color: Theme.avatarText
                    font.family: "serif"
                    font.bold: true
                    font.pixelSize: Theme.scaled(13)
                }
            }
            Label {
                text: root.failed ? "Pi · Error" : "Pi"
                color: root.failed ? Theme.danger : Theme.textBody
                font.pixelSize: Theme.scaled(13)
                font.bold: true
            }
            WorkingIndicator {
                text: root.streaming ? root.workingText : "·  " + root.timeText
                running: root.streaming
                color: Theme.textMuted
                fontPixelSize: Theme.scaled(11)
            }
        }

        TextEdit {
            Layout.fillWidth: true
            text: root.markdownReparsePass ? ""
                  : (root.messageText.length > 0 ? root.messageText : "…")
            // 流式阶段避免每批增量重解析整段 Markdown，结束后再渲染格式。
            textFormat: root.streaming ? TextEdit.PlainText : TextEdit.MarkdownText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: root.failed ? Theme.dangerStrong : Theme.textBody
            font.pixelSize: Theme.scaled(15)
            /** 字号绑定生效后再导入原文，防止代码块沿用上一档字号。 */
            onFontChanged: root.forceMarkdownReparse()
            // 使用平台原生渲染：与系统 ClearType 一致，选中/重绘时字重保持稳定，
            // 同时避免距离场渲染把 Markdown 粗体画得发虚。
            renderType: TextEdit.NativeRendering
            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }
}
