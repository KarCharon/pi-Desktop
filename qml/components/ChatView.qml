pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

/**
 * 以文档阅读方式展示长会话，消息随中间栏宽度伸缩并保留两侧阅读边距。
 */
ListView {
    id: view
    clip: true
    spacing: 22
    topMargin: 26
    bottomMargin: 18
    boundsBehavior: Flickable.StopAtBounds
    property string workingText: "Thinking"
    property bool followTail: true
    ScrollBar.vertical: WorkspaceScrollBar { id: verticalBar }

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
    Component.onCompleted: console.info("[ChatLayout] initialized; widthMode=fill; sideMargin=26; maximumWidth=none")

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
        color: "#9b958b"
        font.pixelSize: 18
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
