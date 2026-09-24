import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 默认仅展示工作目录与上下文统计，保留可注入的 Outline 和 Files 扩展接口。
 */
Rectangle {
    id: root
    required property string workspacePath
    required property string sessionName
    property var sessionStats: ({})
    // 未来模块通过组件注入启用；未提供组件时不创建控件、不占用布局空间。
    property Component outlineContent: null
    property Component filesContent: null

    Component.onCompleted: console.info("[ContextPanel] initialized; outlineEnabled=" + (outlineContent !== null)
                                        + "; filesEnabled=" + (filesContent !== null) + "; contextEnabled=true; addContextButton=false")
    readonly property var contextUsage: sessionStats.contextUsage || ({})
    readonly property var tokenUsage: sessionStats.tokens || ({})
    readonly property bool contextKnown: typeof contextUsage.tokens === "number"
                                         && typeof contextUsage.percent === "number"
                                         && contextUsage.contextWindow > 0
    color: Theme.surface
    border.color: Theme.borderSoft

    ScrollView {
        anchors.fill: parent
        clip: true
        // 面板只允许纵向滚动，避免窄窗口产生额外的原生横向滚动条。
        contentWidth: root.width
        ScrollBar.vertical: WorkspaceScrollBar {}
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: panelColumn
            width: root.width - 28
            x: 14
            y: 18
            spacing: 0

            Loader {
                objectName: "outlineExtension"
                Layout.fillWidth: true
                active: root.outlineContent !== null
                visible: active
                sourceComponent: root.outlineContent
            }

            Loader {
                objectName: "filesExtension"
                Layout.fillWidth: true
                active: root.filesContent !== null
                visible: active
                sourceComponent: root.filesContent
            }

            // 标题只展示名称，不再保留未实现的添加入口及按钮占位。
            Label {
                Layout.fillWidth: true
                text: "Context"
                color: Theme.textTitle
                font.pixelSize: 17
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: contextColumn.implicitHeight + 24
                radius: 10
                color: Theme.surfaceSunken
                border.color: Theme.border

                ColumnLayout {
                    id: contextColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Label { text: "当前项目"; color: Theme.textBody; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: root.workspacePath
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: tokenColumn.implicitHeight + 24
                radius: 10
                color: Theme.surfaceSunken
                border.color: Theme.border

                ColumnLayout {
                    id: tokenColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Label { text: "对话上下文"; color: Theme.textBody; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.contextKnown
                              ? Number(root.contextUsage.tokens).toLocaleString(Qt.locale(), 'f', 0)
                                + " / " + Number(root.contextUsage.contextWindow).toLocaleString(Qt.locale(), 'f', 0)
                                + " tokens（" + Number(root.contextUsage.percent).toFixed(1) + "%）"
                              : "上下文用量未知 / 等待 Pi 更新"
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        visible: root.contextKnown
                        from: 0
                        to: 100
                        value: root.contextKnown ? root.contextUsage.percent : 0
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "会话累计 tokens：" + (root.tokenUsage.total ?? "—")
                              + "\n输入 / 输出：" + (root.tokenUsage.input ?? "—") + " / " + (root.tokenUsage.output ?? "—")
                              + "\n缓存读 / 写：" + (root.tokenUsage.cacheRead ?? "—") + " / " + (root.tokenUsage.cacheWrite ?? "—")
                              + "\n累计费用：" + (typeof root.sessionStats.cost === "number" ? "$" + root.sessionStats.cost.toFixed(4) : "—")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }
            }
        }
    }

    /**
     * 绘制 Outline 中的一项，并通过颜色标记当前位置。
     */
    component OutlineItem: Item {
        id: outlineRoot
        required property string icon
        required property string title
        required property string subtitle
        property bool active: false
        property bool bottomLine: true
        Layout.fillWidth: true
        implicitHeight: 58

        RowLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: outlineRoot.icon
                color: outlineRoot.active ? Theme.accent : Theme.textSecondary
                font.pixelSize: 16
                Layout.preferredWidth: 22
                horizontalAlignment: Text.AlignHCenter
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: outlineRoot.title
                    color: Theme.textBody
                    font.pixelSize: 13
                    font.bold: outlineRoot.active
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: outlineRoot.subtitle
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            visible: outlineRoot.bottomLine
            x: 10
            y: outlineRoot.height - 1
            width: 1
            height: 14
            color: Theme.divider
        }
    }

    /**
     * 绘制相关文件摘要。
     */
    component FileItem: Rectangle {
        id: fileRoot
        required property string icon
        required property string name
        required property string path
        required property color accent
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 62
        radius: 9
        color: "transparent"
        border.color: Theme.borderSoft

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 7
                color: Theme.chipBg
                Label { anchors.centerIn: parent; text: fileRoot.icon; color: fileRoot.accent; font.pixelSize: 16 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { Layout.fillWidth: true; text: fileRoot.name; color: Theme.textBody; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: fileRoot.path; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideMiddle }
            }
            ToolButton { text: "⋯"; implicitWidth: 24; implicitHeight: 24 }
        }
    }
}
