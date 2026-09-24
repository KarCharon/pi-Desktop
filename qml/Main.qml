import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import PiDesktop
import "pages"
import "components"

/**
 * Pi Desktop 主窗口，使用暖中性色三栏工作区承载 Session、对话和上下文。
 */
ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 720
    visible: true
    title: "Pi — " + appController.agent.sessionName
    color: Theme.windowBg

    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent
    property var pendingExtensionRequest: ({})

    // 让全局配色单例跟随用户设置，各组件读取 Theme.* 即可自动刷新。
    Binding {
        target: Theme
        property: "dark"
        value: appController.settings.uiTheme === "dark"
    }

    header: Rectangle {
        implicitHeight: 52
        color: Theme.surface
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 12

            // 页头图标放大到 40 像素，在 52 像素高的栏内保留上下留白。
            Image {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                source: "qrc:/assets/pi-desktop.png"
                fillMode: Image.PreserveAspectFit
                mipmap: true
                Accessible.name: "Pi Desktop"
                // 仅在资源加载完成或失败时记录，便于定位图标缺失问题。
                onStatusChanged: {
                    if (status === Image.Ready)
                        console.info("[AppIcon] 页头图标加载成功；size=40x40；source=" + source)
                    else if (status === Image.Error)
                        console.warn("[AppIcon] 页头图标加载失败；请检查资源是否存在或有效；source=" + source)
                }
            }
            Label {
                text: "Pi"
                color: Theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }
            Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.divider }
            Label {
                text: appController.settings.workspacePath
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideMiddle
                Layout.maximumWidth: 360
            }
            Item { Layout.fillWidth: true }
            Label {
                text: appController.agent.modelName
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 220
            }
            Rectangle {
                implicitWidth: connectionLabel.implicitWidth + 18
                implicitHeight: 25
                radius: 13
                color: appController.agent.connected ? Theme.successSoft : Theme.dangerSoft
                Label {
                    id: connectionLabel
                    anchors.centerIn: parent
                    text: appController.agent.connected ? "Ready" : "Offline"
                    color: appController.agent.connected ? Theme.success : Theme.danger
                    font.pixelSize: 11
                    font.bold: true
                }
            }
            ToolButton {
                text: "⚙"
                font.pixelSize: 17
                implicitWidth: 32
                implicitHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: "连接设置"
                onClicked: {
                    settingsDialog.open()
                }
            }
        }
    }

    ChatPage {
        id: chatPage
        anchors.fill: parent
        chatModel: appController.chatModel
        sessionModel: appController.sessionModel
        projectNavigation: appController
        agent: appController.agent
        workspacePath: appController.settings.workspacePath
        onSettingsRequested: {
            settingsDialog.open()
        }
    }

    footer: Rectangle {
        implicitHeight: 28
        color: Theme.footerBg
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            Label {
                text: "●"
                color: appController.agent.connected ? Theme.success : Theme.danger
                font.pixelSize: 12
            }
            Label {
                text: appController.agent.statusText
                color: Theme.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            Label {
                text: "Profile: " + appController.settings.piProfilePath
                elide: Text.ElideMiddle
                Layout.maximumWidth: 400
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }
    }

    FolderDialog {
        id: workspaceDialog
        property bool applyImmediately: false
        title: "选择 Pi 工作目录"
        onAccepted: {
            if (applyImmediately) {
                appController.applySettings(appController.settings.piExecutable, selectedFolder.toString(),
                                            appController.settings.piProfilePath, appController.settings.proxyUrl)
            } else {
                workspaceField.text = selectedFolder.toString()
            }
            applyImmediately = false
        }
        onRejected: applyImmediately = false
    }

    FolderDialog {
        id: profileDialog
        title: "选择 Pi Profile 目录（包含 settings.json、auth.json 等）"
        onAccepted: profileField.editText = selectedFolder.toString()
    }

    FileDialog {
        id: executableDialog
        title: "选择 Pi 可执行文件或 pi.cmd"
        fileMode: FileDialog.OpenFile
        onAccepted: piExecutableField.editText = selectedFile.toString()
    }

    Dialog {
        id: settingsDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 760)
        // 高度跟随内容，但不超过窗口可用高度；超出部分交给内容区滚动。
        height: Math.min(root.height - 120, implicitHeight)
        title: "Pi 连接设置"
        modal: true
        onOpened: {
            appController.settings.refreshDiscovery()
            piExecutableField.currentIndex = appController.settings.executablePaths.indexOf(appController.settings.piExecutable)
            piExecutableField.editText = appController.settings.piExecutable
            workspaceField.text = appController.settings.workspacePath
            proxyField.text = appController.settings.proxyUrl
            profileField.currentIndex = appController.settings.profileDirectories.indexOf(appController.settings.piProfilePath)
            profileField.editText = appController.settings.piProfilePath
        }
        /** 删除只影响候选列表；共享模型更新后恢复两个编辑框尚未保存的草稿。 */
        function removeOption(kind, path, field) {
            const executable = piExecutableField.editText
            const profile = profileField.editText
            const ok = appController.settings.removeDiscoveryEntry(kind, path)
            piExecutableField.currentIndex = appController.settings.executablePaths.indexOf(executable)
            profileField.currentIndex = appController.settings.profileDirectories.indexOf(profile)
            piExecutableField.editText = executable
            profileField.editText = profile
            field.finishRemoval(ok, appController.settings.discoveryError)
        }
        // 保存失败时保留对话框与草稿，避免标准按钮自动关闭导致看不到错误。
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: settingsDialog.close() }
            Button {
                text: "保存并重连"
                enabled: !appController.agent.busy
                onClicked: {
                    if (appController.applySettings(piExecutableField.editText, workspaceField.text, profileField.editText, proxyField.text))
                        settingsDialog.close()
                }
            }
        }

        // 设置项较多，窗口较矮时整体滚动，避免底部按钮被挤出屏幕。
        ScrollView {
            id: settingsScroll
            anchors.fill: parent
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 10
                Label { text: "界面主题"; color: Theme.textBody }
                RowLayout {
                    spacing: 8
                    Button {
                        text: "浅色"
                        highlighted: appController.settings.uiTheme !== "dark"
                        onClicked: appController.settings.uiTheme = "light"
                    }
                    Button {
                        text: "深色"
                        highlighted: appController.settings.uiTheme === "dark"
                        onClicked: appController.settings.uiTheme = "dark"
                    }
                }
                Label { text: "Pi 命令或可执行文件"; color: Theme.textBody }
                RowLayout {
                    Layout.fillWidth: true
                    RemovablePathComboBox {
                        id: piExecutableField
                        optionKind: "Pi 程序选项"
                        onRemovalConfirmed: path => settingsDialog.removeOption("executable", path, piExecutableField)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        editable: true
                        model: appController.settings.executablePaths
                        Accessible.name: "Pi 命令或可执行文件，可选择或输入"
                    }
                    Button { text: "浏览"; onClicked: executableDialog.open() }
                }
                Label { text: "Agent 工作目录"; color: Theme.textBody }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: workspaceField; Layout.fillWidth: true; placeholderText: "项目目录" }
                    Button {
                        text: "浏览"
                        onClicked: {
                            workspaceDialog.applyImmediately = false
                            workspaceDialog.open()
                        }
                    }
                }
                Label { text: "后台 Pi 代理（HTTP / HTTPS）"; color: Theme.textBody }
                TextField {
                    id: proxyField
                    Layout.fillWidth: true
                    placeholderText: "例如 http://127.0.0.1:7890；留空沿用环境"
                    Accessible.name: "后台 Pi 代理地址"
                    selectByMouse: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    text: "保存并重连后生效，不修改系统代理。保留 NO_PROXY 绕过规则；地址明文保存在本机，请勿填写敏感密码。"
                }
                Label { text: "Pi Profile 目录"; color: Theme.textBody }
                RowLayout {
                    Layout.fillWidth: true
                    RemovablePathComboBox {
                        id: profileField
                        optionKind: "Profile 选项"
                        onRemovalConfirmed: path => settingsDialog.removeOption("profile", path, profileField)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        editable: true
                        model: appController.settings.profileDirectories
                        Accessible.name: "Pi Profile 目录，可选择或输入"
                    }
                    Button { text: "浏览"; onClicked: profileDialog.open() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        text: appController.settings.discoverySummary
                    }
                    Button {
                        text: "重新扫描"
                        onClicked: {
                            // 刷新模型可能重置编辑框，保留尚未保存的自定义草稿。
                            const executable = piExecutableField.editText
                            const profile = profileField.editText
                            appController.settings.refreshDiscovery()
                            piExecutableField.currentIndex = appController.settings.executablePaths.indexOf(executable)
                            profileField.currentIndex = appController.settings.profileDirectories.indexOf(profile)
                            piExecutableField.editText = executable
                            profileField.editText = profile
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.accentText
                    text: "选择 Profile 本身的目录，而不是 sessions 子目录。保存后重连 Pi 并切换会话列表；不会复制或覆盖原 Profile 文件。"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.danger
                    visible: text.length > 0
                    text: appController.agent.busy ? "请先停止当前任务，再保存连接设置。" : appController.settingsError
                }
            }
        }
    }

    Popup {
        id: extensionDialog
        anchors.centerIn: parent
        // 宽度放宽到 900，同时始终给窗口两侧留出 70 像素边距。
        width: Math.min(root.width - 140, 900)
        // 高度跟随内容，但不超过窗口可用高度，长文本在对话框内部消化。
        height: Math.min(root.height - 140, extensionContent.implicitHeight + 36)
        // 内边距归零，内容区完全由下方 18 像素外边距控制，避免样式默认 padding 造成高度溢出。
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: Theme.surface; border.color: Theme.borderStrong; radius: 12 }

        // 标题最多两行，超长标题只省略不撑高，避免挤占输入区。
        readonly property int titleMaxLines: 2
        // 说明区高度上限：窗口越矮越早让步，保证输入区与按钮始终可见。
        // 上限只依赖窗口高度，避免与弹窗自身高度构成绑定循环。
        readonly property int messageMaxHeight: Math.max(56, Math.min(240, root.height - 348))

        ColumnLayout {
            id: extensionContent
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: root.pendingExtensionRequest.title || "Pi 扩展请求"
                color: Theme.textTitle
                font.pixelSize: 17
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: extensionDialog.titleMaxLines
                elide: Text.ElideRight
            }
            // 说明文本可能是长文档，放入可滚动区域并限制高度，避免把按钮顶出弹窗。
            ScrollView {
                id: extensionMessage
                Layout.fillWidth: true
                Layout.maximumHeight: extensionDialog.messageMaxHeight
                visible: extensionMessageLabel.text.length > 0
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                Label {
                    id: extensionMessageLabel
                    width: extensionMessage.availableWidth
                    text: root.pendingExtensionRequest.message || ""
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }
            }
            ComboBox {
                id: extensionSelect
                Layout.fillWidth: true
                visible: root.pendingExtensionRequest.method === "select"
                model: root.pendingExtensionRequest.options || []
            }
            TextArea {
                id: extensionInput
                Layout.fillWidth: true
                // 输入区吃掉剩余高度：editor 默认 320 像素起，窗口越大越高，最小 80 保证仍可编辑。
                Layout.fillHeight: true
                Layout.minimumHeight: 80
                Layout.preferredHeight: root.pendingExtensionRequest.method === "editor" ? 320 : 56
                visible: root.pendingExtensionRequest.method === "input" || root.pendingExtensionRequest.method === "editor"
                placeholderText: root.pendingExtensionRequest.placeholder || "请输入…"
                wrapMode: TextEdit.Wrap
                renderType: TextEdit.NativeRendering
                selectByMouse: true
                focus: visible
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: "取消"
                    onClicked: {
                        appController.agent.respondToExtension(root.pendingExtensionRequest.id, {"cancelled": true})
                        extensionDialog.close()
                    }
                }
                Button {
                    text: root.pendingExtensionRequest.method === "confirm" ? "确认" : "提交"
                    highlighted: true
                    onClicked: {
                        let method = root.pendingExtensionRequest.method
                        let result = method === "confirm" ? {"confirmed": true}
                                   : method === "select" ? {"value": extensionSelect.currentText}
                                   : {"value": extensionInput.text}
                        appController.agent.respondToExtension(root.pendingExtensionRequest.id, result)
                        extensionDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: appController.agent

        /**
         * 打开 Pi 扩展发起的阻塞式对话。
         */
        function onExtensionDialogRequested(request) {
            root.pendingExtensionRequest = request
            extensionInput.text = request.prefill || ""
            // 预填后把光标放到末尾，避免长文本被全选误删。
            extensionInput.cursorPosition = extensionInput.length
            extensionSelect.currentIndex = 0
            console.info("[ExtensionDialog] 打开扩展对话；method=" + request.method
                         + "；prefillLength=" + (request.prefill ? request.prefill.length : 0)
                         + "；预分配尺寸=" + Math.round(extensionDialog.width)
                         + "x" + Math.round(Math.min(root.height - 140, extensionContent.implicitHeight + 36)))
            extensionDialog.open()
        }

        /**
         * 把扩展预填内容同步到 Prompt 编辑器。
         */
        function onEditorTextRequested(text) {
            chatPage.promptText = text
        }
    }
}
