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
    color: "#fbfaf7"
    border.color: "#e9e5de"

    ScrollView {
        id: panelScroll
        anchors.fill: parent
        clip: true
        // 面板只允许纵向滚动，避免窄窗口产生额外的原生横向滚动条。
        contentWidth: root.width
        // ScrollView 样式自带的滚动条靠 parent/x/y/height 手动定位，覆盖时必须补上，
        // 否则滑条会落在左上角且拖不动。
        ScrollBar.vertical: WorkspaceScrollBar {
            parent: panelScroll
            x: panelScroll.mirrored ? 0 : panelScroll.width - width
            y: panelScroll.topPadding
            height: panelScroll.availableHeight
        }
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
                color: "#292824"
                font.pixelSize: 17
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: contextColumn.implicitHeight + 24
                radius: 10
                color: "#f8f6f1"
                border.color: "#e5e0d7"

                ColumnLayout {
                    id: contextColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Label { text: "当前项目"; color: "#3b3934"; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: root.workspacePath
                        color: "#858078"
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
                color: "#f8f6f1"
                border.color: "#e5e0d7"

                ColumnLayout {
                    id: tokenColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Label { text: "对话上下文"; color: "#3b3934"; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.contextKnown
                              ? Number(root.contextUsage.tokens).toLocaleString(Qt.locale(), 'f', 0)
                                + " / " + Number(root.contextUsage.contextWindow).toLocaleString(Qt.locale(), 'f', 0)
                                + " tokens（" + Number(root.contextUsage.percent).toFixed(1) + "%）"
                              : "上下文用量未知 / 等待 Pi 更新"
                        color: "#858078"
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
                        color: "#858078"
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
                color: outlineRoot.active ? "#bb7050" : "#73716a"
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
                    color: "#393733"
                    font.pixelSize: 13
                    font.bold: outlineRoot.active
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: outlineRoot.subtitle
                    color: "#938f87"
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
            color: "#ddd8cf"
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
        border.color: "#e8e3da"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 7
                color: "#f1eee8"
                Label { anchors.centerIn: parent; text: fileRoot.icon; color: fileRoot.accent; font.pixelSize: 16 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { Layout.fillWidth: true; text: fileRoot.name; color: "#48453f"; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: fileRoot.path; color: "#969189"; font.pixelSize: 10; elide: Text.ElideMiddle }
            }
            ToolButton { text: "⋯"; implicitWidth: 24; implicitHeight: 24 }
        }
    }
}
