import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "../components"

/**
 * 三栏工作区页面，组合 Session 导航、文档式对话、Prompt 和上下文面板。
 */
Item {
    id: page

    required property var chatModel
    required property var sessionModel
    required property var agent
    required property string workspacePath
    property var projectNavigation: null
    property alias promptText: promptEditor.text
    signal settingsRequested()
    property bool leftSidebarVisible: false
    property bool rightSidebarVisible: false

    /** 统一工作区导航按钮，使用几何线框图标，避免系统字体符号和原生样式差异。 */
    component NavigationButton: Basic.Button {
        id: navigation
        property string iconKind: "left"
        property bool selected: false
        property bool accent: false
        property string hint: text
        readonly property color ink: !enabled ? "#b9b1a6"
                                             : selected || accent ? "#a96346" : "#736b60"
        implicitHeight: 34
        implicitWidth: navigationContent.implicitWidth + 24
        leftPadding: 12
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: hint
        ToolTip.visible: hovered
        ToolTip.delay: 550
        ToolTip.text: hint

        background: Rectangle {
            radius: 9
            color: !navigation.enabled ? "#f5f2ed"
                 : navigation.down ? "#e8d7c8"
                 : navigation.selected ? "#f0e0d3"
                 : navigation.hovered ? "#eee7dd"
                 : navigation.accent ? "#f6ece3" : "#f7f4ef"
            border.width: 1
            border.color: navigation.visualFocus ? "#bd7957"
                        : navigation.selected ? "#dfbba2"
                        : navigation.hovered ? "#d8cabb" : "#e8e0d5"
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Row {
            id: navigationContent
            spacing: 7
            Item {
                width: 16
                height: navigation.availableHeight
                // 侧栏图标按方向镜像，不使用依赖字体的特殊字符。
                Rectangle {
                    visible: navigation.iconKind !== "plus"
                    anchors.centerIn: parent
                    width: 15
                    height: 13
                    radius: 3
                    color: "transparent"
                    border.color: navigation.ink
                    Rectangle {
                        x: navigation.iconKind === "right" ? 9 : 4
                        y: 1
                        width: 1
                        height: 11
                        color: navigation.ink
                    }
                }
                Rectangle {
                    visible: navigation.iconKind === "plus"
                    anchors.centerIn: parent
                    width: 12
                    height: 1.5
                    radius: 1
                    color: navigation.ink
                }
                Rectangle {
                    visible: navigation.iconKind === "plus"
                    anchors.centerIn: parent
                    width: 1.5
                    height: 12
                    radius: 1
                    color: navigation.ink
                }
            }
            Text {
                text: navigation.text
                height: navigation.availableHeight
                verticalAlignment: Text.AlignVCenter
                color: navigation.ink
                font.pixelSize: 12
                font.weight: navigation.selected ? Font.DemiBold : Font.Medium
            }
        }
    }

    Connections {
        target: page.agent
        /** 合并取回队列与当前草稿，避免覆盖用户正在输入的内容。 */
        function onRestoreDraftRequested(text) {
            promptEditor.text = text + (promptEditor.text.length ? "\n\n" + promptEditor.text : "")
        }
        /** 新会话确认创建成功后进入专注布局，取消或失败不改变侧栏。 */
        function onNewSessionCreated() {
            page.leftSidebarVisible = false
            page.rightSidebarVisible = false
            console.info("[WorkspaceChrome] new session created; sidebars=collapsed")
        }
    }

    // 只在页面创建时记录样式配置，避免滚动和拖动回调持续刷日志。
    Component.onCompleted: console.info("[WorkspaceChrome] initialized; scrollbar=custom; hitWidth=12; thumbWidth=6; splitter=Basic; sessionMenu=false; navigation=warm-outline; focusRing=true")

    Basic.SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // 分隔条保留拖动命中宽度，只显示一像素浅色线，消除原生粗黑边。
        handle: Rectangle {
            implicitWidth: 6
            implicitHeight: 6
            color: SplitHandle.pressed ? "#e7ddd1"
                                      : SplitHandle.hovered ? "#f0ebe3" : "#faf9f6"
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: parent.height
                color: "#e5e0d7"
            }
        }

        SessionSidebar {
            objectName: "sessionSidebar"
            visible: page.leftSidebarVisible
            SplitView.preferredWidth: 282
            SplitView.minimumWidth: 230
            SplitView.maximumWidth: 360
            model: page.sessionModel
            activeSessionPath: page.agent.sessionFile
            projects: page.projectNavigation ? page.projectNavigation.projects : null
            workspacePath: page.workspacePath
            navigationError: page.projectNavigation ? page.projectNavigation.workspaceError : ""
            switching: page.projectNavigation ? page.projectNavigation.workspaceSwitching : false
            sessionConnected: page.agent.connected
            sessionActionsEnabled: !page.agent.busy && !switching
            onWorkspaceAdditionRequested: (projectId, path) => {
                if (page.projectNavigation)
                    page.projectNavigation.addProjectFolder(projectId, path)
            }
            onWorkspaceSelected: path => {
                if (page.projectNavigation)
                    page.projectNavigation.openWorkspace(path, "")
            }
            onSessionSelected: (path, workspace) => {
                if (page.projectNavigation)
                    page.projectNavigation.openWorkspace(workspace, path)
                else
                    page.agent.switchSession(path)
            }
            onNewSessionRequested: page.agent.newSession()
            onRefreshRequested: page.sessionModel.refresh()
            onSettingsRequested: page.settingsRequested()
        }

        Rectangle {
            SplitView.fillWidth: true
            color: "#faf9f6"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: "#faf9f6"
                    border.color: "#ebe7df"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 8
                        NavigationButton {
                            objectName: "toggleSessions"
                            text: "会话"
                            selected: page.leftSidebarVisible
                            hint: page.leftSidebarVisible ? "收起会话列表" : "展开会话列表"
                            onClicked: page.leftSidebarVisible = !page.leftSidebarVisible
                        }
                        NavigationButton {
                            objectName: "newConversation"
                            text: "新对话"
                            iconKind: "plus"
                            accent: true
                            hint: page.agent.busy ? "请等待当前任务结束后新建对话" : "新建对话"
                            enabled: page.agent.connected && !page.agent.busy
                            onClicked: page.agent.newSession()
                        }
                        Label {
                            text: page.agent.sessionName
                            color: "#34312c"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.leftMargin: 8
                            Layout.minimumWidth: 0
                            Layout.fillWidth: true
                        }
                        NavigationButton {
                            objectName: "toggleContext"
                            text: "上下文"
                            iconKind: "right"
                            selected: page.rightSidebarVisible
                            hint: page.rightSidebarVisible ? "收起上下文面板" : "展开上下文面板"
                            onClicked: page.rightSidebarVisible = !page.rightSidebarVisible
                        }
                    }
                }

                ChatView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: page.chatModel
                    workingText: page.agent.workingText
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    WorkingIndicator {
                        Layout.fillWidth: true
                        // 普通提示使用动态三点，工具、重试和错误仍显示真实状态。
                        text: !page.agent.busy ? ""
                              : page.agent.statusText === page.agent.workingText + "…"
                                ? page.agent.workingText : page.agent.statusText
                        running: page.agent.busy
                        color: "#a77c64"
                    }
                    Label {
                        // 收起上下文侧栏后仍保留占用摘要；未知值不显示为零。
                        text: contextPanel.contextKnown
                              ? "Context " + Number(contextPanel.contextUsage.percent).toFixed(1) + "% · "
                                + contextPanel.contextUsage.tokens + " / " + contextPanel.contextUsage.contextWindow
                              : "Context —"
                        color: "#858078"
                        font.pixelSize: 11
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.preferredHeight: visible ? Math.min(90, queueLabel.implicitHeight) : 0
                    visible: !!page.agent.queueText
                    Label {
                        id: queueLabel
                        width: parent.width
                        text: page.agent.queueText || ""
                        wrapMode: Text.Wrap
                        color: "#a77c64"
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: promptEditor.implicitHeight + 38
                    color: "#faf9f6"

                    PromptEditor {
                        id: promptEditor
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        anchors.topMargin: 8
                        anchors.bottomMargin: 16
                        busy: page.agent.busy
                        connected: page.agent.connected
                        onSubmit: (text, followUp) => {
                            if (page.agent.prompt(text, followUp))
                                promptEditor.text = ""
                        }
                        onRetrieveRequested: page.agent.retrieveQueue()
                        onAbortRequested: page.agent.abort()
                    }
                }
            }
        }

        ContextPanel {
            id: contextPanel
            objectName: "contextPanel"
            visible: page.rightSidebarVisible
            sessionStats: page.agent.sessionStats
            SplitView.preferredWidth: 306
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 380
            workspacePath: page.workspacePath
            sessionName: page.agent.sessionName
        }
    }
}
