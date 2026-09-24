pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 以低干扰的文档行展示 Tool Call，默认折叠详细输入和输出。
 */
Rectangle {
    id: root
    required property string name
    required property string inputText
    required property string outputText
    required property string toolState
    required property bool failed
    property bool expanded: failed
    // 运行失败时自动展开详情；用户之后仍可手动折叠。
    onFailedChanged: {
        if (failed)
            expanded = true
    }

    implicitHeight: toolColumn.implicitHeight + Theme.scaled(20)
    radius: Theme.scaled(9)
    color: Theme.surface
    border.color: root.failed ? Theme.dangerBorder
                : root.toolState === "running" ? Theme.warningBorder : Theme.borderStrong

    ColumnLayout {
        id: toolColumn
        anchors.fill: parent
        anchors.margins: Theme.scaled(10)
        spacing: Theme.scaled(8)

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.expanded ? "⌄" : "›"
                color: Theme.textMuted
                font.pixelSize: Theme.scaled(18)
                Layout.preferredWidth: Theme.scaled(16)
            }
            Label {
                text: root.name || "Tool"
                color: Theme.textBody
                font.pixelSize: Theme.scaled(13)
                font.bold: true
            }
            Label {
                text: root.inputText ? "  " + root.inputText.replace(/\s+/g, " ").trim().slice(0, 72) : ""
                color: Theme.textMuted
                font.pixelSize: Theme.scaled(11)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: root.failed ? "失败" : root.toolState === "running" ? "运行中" : "✓"
                color: root.failed ? Theme.danger
                     : root.toolState === "running" ? Theme.warning : Theme.successText
                font.pixelSize: Theme.scaled(12)
                font.bold: true
            }
        }

        Loader {
            id: detailsLoader
            Layout.fillWidth: true
            active: root.expanded
            visible: active
            sourceComponent: ColumnLayout {
                width: detailsLoader.width
                spacing: Theme.scaled(6)
                Label { text: "Input"; color: Theme.textMuted; font.pixelSize: Theme.scaled(11) }
                // TextArea 不是 Flickable，直接挂 ScrollBar 非法；包一层 ScrollView 提供可拖动的滑条。
                // 框高仍随内容增长、到上限为止，超出部分用滑条上下查看。
                ScrollView {
                    id: inputScroll
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(Theme.scaled(180), Math.max(Theme.scaled(48), inputArea.contentHeight + Theme.scaled(14)))
                    padding: 0
                    contentWidth: availableWidth
                    // ScrollView 样式自带的滚动条靠 parent/x/y/height 手动定位，覆盖时必须补上。
                    ScrollBar.vertical: WorkspaceScrollBar {
                        parent: inputScroll
                        x: inputScroll.mirrored ? 0 : inputScroll.width - width
                        y: inputScroll.topPadding
                        height: inputScroll.availableHeight
                    }
                    background: Rectangle { color: Theme.codeBg; radius: Theme.scaled(6) }
                    TextArea {
                        id: inputArea
                        width: inputScroll.availableWidth
                        text: root.inputText || "(empty)"
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: Theme.textBody
                        background: null
                        font.family: "monospace"
                        font.pixelSize: Theme.scaled(11)
                        renderType: TextEdit.NativeRendering
                    }
                }
                Label { text: "Output"; color: Theme.textMuted; font.pixelSize: Theme.scaled(11) }
                // 输出可能很长，同样用 ScrollView 提供滑条，框高行为保持不变。
                ScrollView {
                    id: outputScroll
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(Theme.scaled(260), Math.max(Theme.scaled(48), outputArea.contentHeight + Theme.scaled(14)))
                    padding: 0
                    contentWidth: availableWidth
                    // ScrollView 样式自带的滚动条靠 parent/x/y/height 手动定位，覆盖时必须补上。
                    ScrollBar.vertical: WorkspaceScrollBar {
                        parent: outputScroll
                        x: outputScroll.mirrored ? 0 : outputScroll.width - width
                        y: outputScroll.topPadding
                        height: outputScroll.availableHeight
                    }
                    background: Rectangle { color: Theme.codeBg; radius: Theme.scaled(6) }
                    TextArea {
                        id: outputArea
                        width: outputScroll.availableWidth
                        text: root.outputText || (root.toolState === "running" ? "等待输出…" : "(empty)")
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: root.failed ? Theme.dangerStrong : Theme.textBody
                        background: null
                        font.family: "monospace"
                        font.pixelSize: Theme.scaled(11)
                        renderType: TextEdit.NativeRendering
                    }
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.expanded = !root.expanded
    }
}
