pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

/**
 * 以文档阅读方式展示长会话，消息随中间栏宽度伸缩并保留两侧阅读边距。
 *
 * 支持 Ctrl+滚轮缩放对话字号与间距（Ctrl+0 复位），缩放系数存放于 Theme 单例，
 * 因此所有消息组件的文字会同步放大缩小；列表的横向阅读边距保持不变，缩放只影响内容。
 */
ListView {
    id: view
    clip: true
    spacing: Theme.scaled(22)
    topMargin: Theme.scaled(26)
    bottomMargin: Theme.scaled(18)
    boundsBehavior: Flickable.StopAtBounds
    property string workingText: "Thinking"
    property bool followTail: true
    ScrollBar.vertical: WorkspaceScrollBar { id: verticalBar }

    // Ctrl+滚轮只用于缩放对话；未按 Ctrl 的滚轮不激活本处理器，仍交给列表滚动。
    WheelHandler {
        objectName: "chatZoomWheel"
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => view.applyZoom(event.angleDelta.y)
    }

    // Ctrl+0 恢复默认字号，方便缩放过小或过大后一键复位。
    Shortcut {
        sequences: ["Ctrl+0"]
        onActivated: view.setZoom(1.0)
    }

    /**
     * 按滚轮方向步进缩放：向上放大、向下缩小，统一交给 setZoom 夹取范围。
     */
    function applyZoom(delta) {
        if (delta === 0)
            return
        const step = delta > 0 ? Theme.chatZoomStep : 1 / Theme.chatZoomStep
        setZoom(Theme.chatZoom * step)
    }

    /**
     * 设置对话缩放并尽量保持当前阅读位置。
     *
     * 末尾跟随时缩放后继续贴底；否则按视口中心在全文中的比例恢复位置。
     * 字号变化要到下一帧才反映到 contentHeight，因此用 callLater 延后修正。
     */
    function setZoom(value) {
        const target = Math.max(Theme.chatZoomMinimum,
                                Math.min(Theme.chatZoomMaximum, value))
        if (Math.abs(target - Theme.chatZoom) < 0.001)
            return
        const anchor = contentHeight > 0 ? (contentY + height / 2) / contentHeight : 0.5
        const keepTail = followTail || atYEnd
        Theme.chatZoom = target
        Qt.callLater(() => {
            if (keepTail)
                view.positionViewAtEnd()
            else if (view.contentHeight > 0)
                view.contentY = Math.max(0, anchor * view.contentHeight - view.height / 2)
        })
    }

    // 用户翻阅历史时不抢滚动位置；主动回到底部后恢复跟随。
    onContentYChanged: {
        if (dragging || flicking || verticalBar.pressed)
            followTail = atYEnd
    }
    onMovementEnded: followTail = atYEnd

    Timer {
        id: tailTimer
        interval: 50
        onTriggered: {
            if (view.followTail && !view.moving && !verticalBar.pressed)
                view.positionViewAtEnd()
        }
    }

    // 只在视图创建时记录布局策略，避免流式更新和窗口缩放时持续刷日志。
    Component.onCompleted: console.info("[ChatLayout] initialized; widthMode=fill; sideMargin=26; maximumWidth=none; zoomShortcut=Ctrl+wheel")

    delegate: Item {
        id: delegateLoader
        width: view.width
        implicitHeight: messageLoader.height

        required property string entryType
        required property string messageRole
        required property string content
        required property string itemState
        required property string toolName
        required property string toolInput
        required property string toolOutput
        required property bool isError
        required property string timestamp

        // ListView 管理外层行的位置；内层 Loader 保持居中，避免列表布局覆盖横向边距。
        Loader {
            id: messageLoader
            width: Math.max(0, delegateLoader.width - 52)
            anchors.horizontalCenter: parent.horizontalCenter
            sourceComponent: delegateLoader.entryType === "tool" ? toolComponent
                             : delegateLoader.messageRole === "user" ? userComponent
                             : delegateLoader.messageRole === "assistant" ? assistantComponent
                             : systemComponent
        }

        Component {
            id: userComponent
            UserMessage {
                width: messageLoader.width
                messageText: delegateLoader.content
                timeText: delegateLoader.timestamp
            }
        }
        Component {
            id: assistantComponent
            AssistantMessage {
                width: messageLoader.width
                messageText: delegateLoader.content
                timeText: delegateLoader.timestamp
                streaming: delegateLoader.itemState === "streaming"
                workingText: view.workingText
                failed: delegateLoader.isError
            }
        }
        Component {
            id: systemComponent
            SystemMessage {
                width: messageLoader.width
                messageText: delegateLoader.content
                failed: delegateLoader.isError
            }
        }
        Component {
            id: toolComponent
            ToolCall {
                width: messageLoader.width
                name: delegateLoader.toolName
                inputText: delegateLoader.toolInput
                outputText: delegateLoader.toolOutput
                toolState: delegateLoader.itemState
                failed: delegateLoader.isError
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: view.count === 0
        text: "开始一段新的 Pi 会话"
        color: Theme.textMuted
        font.pixelSize: Theme.scaled(18)
    }

    Connections {
        target: view.model

        /**
         * 合并自动滚动请求，仅在用户仍跟随末尾时滚动。
         */
        function onContentUpdated() {
            if (view.followTail && !tailTimer.running)
                tailTimer.start()
        }

        /** 切换或清空会话后恢复自动跟随。 */
        function onModelReset() {
            view.followTail = true
            tailTimer.restart()
        }
    }
}
