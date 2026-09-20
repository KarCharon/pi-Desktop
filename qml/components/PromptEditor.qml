import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 参考设计稿提供文档式 Composer，支持 Enter 发送和 Shift+Enter 换行。
 * 同时提供 Pi 命令的 `/` 自动补全、附件芯片以及拖放添加文件。
 */
Item {
    id: root
    property alias text: editor.text
    required property bool busy
    required property bool connected
    property var attachments: []
    property var commands: []
    // 宿主传入 undefined 时回退为空列表，避免绑定报 TypeError。
    readonly property var attachmentItems: attachments || []
    readonly property var commandItems: commands || []
    property var filteredCommands: []
    property int commandIndex: 0
    property bool commandPopupVisible: false
    signal submit(string text, bool followUp)
    signal retrieveRequested()
    signal abortRequested()
    signal attachRequested()
    signal attachmentRemoved(int index)
    signal filesDropped(var urls)

    implicitHeight: Math.min(240, Math.max(94, editor.contentHeight + 42
                                            + (attachmentItems.length > 0 ? 34 : 0)))

    /**
     * 根据光标前的 `/token` 过滤可用命令并控制补全弹层。
     */
    function updateCommandPopup() {
        if (!editor.activeFocus) {
            commandPopupVisible = false
            return
        }
        const before = editor.text.substring(0, editor.cursorPosition)
        const lineStart = Math.max(before.lastIndexOf("\n"), before.lastIndexOf("\r")) + 1
        const token = before.substring(lineStart)
        if (token.length > 0 && token.charAt(0) === "/"
                && token.indexOf(" ") < 0 && token.indexOf("\t") < 0) {
            const query = token.substring(1).toLowerCase()
            const matched = []
            for (let i = 0; i < commandItems.length; ++i) {
                const command = commandItems[i]
                if (query.length === 0 || command.name.toLowerCase().startsWith(query))
                    matched.push(command)
            }
            filteredCommands = matched.slice(0, 8)
            commandIndex = 0
            commandPopupVisible = filteredCommands.length > 0
        } else {
            commandPopupVisible = false
        }
    }

    /**
     * 用所选命令替换当前 `/token`，保留行内其余文本。
     */
    function applyCommand(name) {
        const full = editor.text
        const before = full.substring(0, editor.cursorPosition)
        const lineStart = Math.max(before.lastIndexOf("\n"), before.lastIndexOf("\r")) + 1
        const after = full.substring(editor.cursorPosition)
        let end = after.search(/[\s]/)
        if (end < 0)
            end = after.length
        const insert = "/" + name + " "
        editor.text = full.substring(0, lineStart) + insert + after.substring(end)
        editor.cursorPosition = lineStart + insert.length
        commandPopupVisible = false
        editor.forceActiveFocus()
    }

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
                placeholderText: root.connected ? "Ask pi… 输入 / 使用命令" : "正在连接 Pi…"
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
                selectByMouse: true

                onTextChanged: root.updateCommandPopup()
                onCursorPositionChanged: root.updateCommandPopup()
                onActiveFocusChanged: {
                    if (!activeFocus)
                        root.commandPopupVisible = false
                }

                Keys.onPressed: event => {
                    if (root.commandPopupVisible
                            && (event.modifiers & Qt.AltModifier) === 0) {
                        if (event.key === Qt.Key_Down) {
                            root.commandIndex = Math.min(root.commandIndex + 1,
                                                         root.filteredCommands.length - 1)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Up) {
                            root.commandIndex = Math.max(root.commandIndex - 1, 0)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                || event.key === Qt.Key_Tab) {
                            root.applyCommand(root.filteredCommands[root.commandIndex].name)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            root.commandPopupVisible = false
                            event.accepted = true
                            return
                        }
                    }
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

            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: root.attachmentItems.length > 0
                Repeater {
                    model: root.attachmentItems
                    delegate: Rectangle {
                        height: 26
                        width: chipLayout.implicitWidth + 20
                        radius: 7
                        color: "#f1eee8"
                        border.color: "#e2ddd5"
                        RowLayout {
                            id: chipLayout
                            anchors.centerIn: parent
                            spacing: 5
                            Label {
                                text: modelData.image ? "□" : "▤"
                                color: "#a96346"
                                font.pixelSize: 12
                            }
                            Label {
                                text: modelData.name
                                color: "#5c5850"
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 170
                            }
                            Label {
                                text: "×"
                                color: "#a96346"
                                font.pixelSize: 14
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.attachmentRemoved(index)
                                }
                            }
                        }
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
                    ToolTip.text: "添加文件或图片"
                    enabled: root.connected
                    background: Rectangle { color: addButton.down ? "#ede8e0" : "#f7f5f0"; border.color: "#e5dfd6"; radius: 8 }
                    onClicked: root.attachRequested()
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

    Rectangle {
        id: commandPopup
        visible: root.commandPopupVisible
        z: 20
        width: editor.width
        height: Math.min(240, commandList.contentHeight + 8)
        x: editor.mapToItem(root, 0, 0).x
        y: editor.mapToItem(root, 0, 0).y - height - 6
        radius: 9
        color: "#ffffff"
        border.color: "#d8d1c7"

        ListView {
            id: commandList
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: root.filteredCommands
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                width: ListView.view.width
                height: 32
                radius: 6
                color: index === root.commandIndex ? "#f0e0d3" : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    Label {
                        text: modelData.invocation
                        color: "#a96346"
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.description
                        color: "#7a756d"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    Label {
                        text: modelData.source === "desktop" ? "内置" : modelData.source
                        color: "#b3ada4"
                        font.pixelSize: 9
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.commandIndex = index
                    onClicked: root.applyCommand(modelData.name)
                }
            }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (drop.hasUrls) {
                root.filesDropped(drop.urls)
                drop.acceptProposedAction()
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
