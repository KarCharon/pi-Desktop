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

    implicitHeight: toolColumn.implicitHeight + 20
    radius: 9
    color: "#fbfaf7"
    border.color: root.failed ? "#d9968d" : root.toolState === "running" ? "#d9b47d" : "#e2ddd4"

    ColumnLayout {
        id: toolColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.expanded ? "⌄" : "›"
                color: "#8b857b"
                font.pixelSize: 18
                Layout.preferredWidth: 16
            }
            Label {
                text: root.name || "Tool"
                color: "#403d37"
                font.pixelSize: 13
                font.bold: true
            }
            Label {
                text: root.inputText ? "  " + root.inputText.replace(/\s+/g, " ").trim().slice(0, 72) : ""
                color: "#8d877e"
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: root.failed ? "失败" : root.toolState === "running" ? "运行中" : "✓"
                color: root.failed ? "#b65d55" : root.toolState === "running" ? "#b48445" : "#559467"
                font.pixelSize: 12
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
                spacing: 6
                Label { text: "Input"; color: "#9a948b"; font.pixelSize: 11 }
                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(180, Math.max(48, contentHeight + 14))
                    text: root.inputText || "(empty)"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: "#59554e"
                    background: Rectangle { color: "#f3f0e9"; radius: 6 }
                    font.family: "monospace"
                    font.pixelSize: 11
                }
                Label { text: "Output"; color: "#9a948b"; font.pixelSize: 11 }
                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(260, Math.max(48, contentHeight + 14))
                    text: root.outputText || (root.toolState === "running" ? "等待输出…" : "(empty)")
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: root.failed ? "#a34e49" : "#59554e"
                    background: Rectangle { color: "#f3f0e9"; radius: 6 }
                    font.family: "monospace"
                    font.pixelSize: 11
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.expanded = !root.expanded
    }
}
