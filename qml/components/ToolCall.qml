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
                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(Theme.scaled(180), Math.max(Theme.scaled(48), contentHeight + Theme.scaled(14)))
                    text: root.inputText || "(empty)"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: Theme.textBody
                    background: Rectangle { color: Theme.codeBg; radius: Theme.scaled(6) }
                    font.family: "monospace"
                    font.pixelSize: Theme.scaled(11)
                    renderType: TextEdit.NativeRendering
                }
                Label { text: "Output"; color: Theme.textMuted; font.pixelSize: Theme.scaled(11) }
                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(Theme.scaled(260), Math.max(Theme.scaled(48), contentHeight + Theme.scaled(14)))
                    text: root.outputText || (root.toolState === "running" ? "等待输出…" : "(empty)")
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: root.failed ? Theme.dangerStrong : Theme.textBody
                    background: Rectangle { color: Theme.codeBg; radius: Theme.scaled(6) }
                    font.family: "monospace"
                    font.pixelSize: Theme.scaled(11)
                    renderType: TextEdit.NativeRendering
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.expanded = !root.expanded
    }
}
