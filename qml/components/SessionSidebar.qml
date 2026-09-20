pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs

/** 保留暖色会话栏样式，以独立项目和真实工作文件夹组织会话。 */
Rectangle {
    id: root
    required property var model
    required property string activeSessionPath
    property var projects: null
    property string workspacePath: ""
    property string navigationError: ""
    property bool switching: false
    property bool sessionActionsEnabled: true
    property bool sessionConnected: true
    property string searchQuery: ""
    property var collapsedFolders: ({})
    property var menuEntry: ({})
    property var removalEntry: ({})
    signal sessionSelected(string path, string workspace)
    signal workspaceSelected(string path)
    signal workspaceAdditionRequested(string projectId, string path)
    signal newSessionRequested()
    signal refreshRequested()
    signal settingsRequested()

    /** 稳定编号控制展开状态，折叠不会切换工作目录。 */
    function toggleFolder(id) {
        let next = Object.assign({}, collapsedFolders)
        next[id] = !next[id]
        collapsedFolders = next
        console.info("[Projects] 折叠状态变化；id=" + id + "; collapsed=" + next[id])
    }

    /** 搜索时显示匹配项及祖先；平时按两级折叠状态显示。 */
    function rowVisible(row) {
        if (searchQuery.length)
            return (row.searchText || "").toLowerCase().includes(searchQuery)
        return (row.kind === "project" || !collapsedFolders[row.projectId])
                && (row.kind !== "session" || !collapsedFolders[row.folderId])
    }

    /** 统一路径比较用于选中显示，真实校验仍由后端执行。 */
    function samePath(a, b) {
        let left = a.replace(/\\/g, "/").replace(/\/$/, "")
        let right = b.replace(/\\/g, "/").replace(/\/$/, "")
        return Qt.platform.os === "windows" ? left.toLowerCase() === right.toLowerCase() : left === right
    }

    /** 打开项目新增或显示名称编辑框。 */
    function editName(entry) {
        menuEntry = entry
        nameField.text = entry.title || ""
        nameDialog.open()
        nameField.forceActiveFocus()
    }

    /** 独立保存删除目标并二次确认，后续菜单操作不改变待删除项。 */
    function requestRemoval(entry) {
        removalEntry = {id: entry.id, title: entry.title, kind: entry.kind}
        removeDialog.open()
        console.info("[Projects] 请求删除确认；id=" + entry.id + "; kind=" + entry.kind)
    }

    color: "#f3f0e9"
    border.color: "#e7e2d8"

    /** 沿用会话栏暖色按钮，避免新增导航出现原生深色背景。 */
    component SidebarButton: Basic.Button {
        id: control
        implicitHeight: 30
        implicitWidth: 30
        padding: 6
        hoverEnabled: true
        contentItem: Text {
            text: control.text
            color: control.enabled ? "#786451" : "#b8afa3"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: control.down ? "#e1d8cb" : control.hovered ? "#e9e3d9" : "transparent"
            border.color: control.visualFocus ? "#c47a59" : "transparent"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 0

        Basic.Button {
            id: newChatButton
            enabled: root.sessionActionsEnabled && root.sessionConnected
            Layout.fillWidth: true
            Layout.topMargin: 16
            Layout.bottomMargin: 16
            implicitHeight: 48
            text: "+   新建对话"
            contentItem: Text {
                text: newChatButton.text
                color: "#ffffff"
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: newChatButton.down ? "#ae6548" : "#c47a59"; radius: 9; opacity: newChatButton.enabled ? 1 : 0.5 }
            onClicked: root.newSessionRequested()
        }

        TextField {
            id: searchField
            objectName: "sessionSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            placeholderText: "搜索项目、文件夹或对话…"
            leftPadding: 14
            color: "#4b4842"
            placeholderTextColor: "#969087"
            background: Rectangle {
                color: "#faf9f6"
                border.color: searchField.activeFocus ? "#c47a59" : "#e1dcd3"
                radius: 9
            }
            onTextChanged: root.searchQuery = text.trim().toLowerCase()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 22
            Layout.bottomMargin: 8
            Label {
                text: "项目"
                color: "#393631"
                font.pixelSize: 14
                font.bold: true
                Layout.fillWidth: true
            }
            BusyIndicator {
                implicitWidth: 22
                implicitHeight: 22
                running: root.model.loading || root.switching
                visible: running
            }
            SidebarButton {
                objectName: "addProject"
                text: "+"
                enabled: !!root.projects && !root.switching
                ToolTip.visible: hovered
                ToolTip.text: "添加项目"
                Accessible.name: "添加项目"
                onClicked: root.editName({ kind: "project", id: "", title: "" })
            }
            SidebarButton {
                text: "↻"
                enabled: !root.model.loading
                ToolTip.visible: hovered
                ToolTip.text: "刷新历史会话"
                onClicked: root.refreshRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.bottomMargin: visible ? 8 : 0
            visible: text.length > 0
            text: root.navigationError || (root.projects ? root.projects.error : "")
            color: "#a95137"
            wrapMode: Text.Wrap
            font.pixelSize: 11
        }

        ListView {
            id: sessionList
            objectName: "recentSessions"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.projects ? root.projects.rows : []
            clip: true
            spacing: 0
            bottomMargin: 8
            ScrollBar.vertical: WorkspaceScrollBar {}

            delegate: Rectangle {
                id: entry
                required property var modelData
                readonly property bool isSession: modelData.kind === "session"
                readonly property bool isFolder: modelData.kind === "folder"
                readonly property bool matches: root.rowVisible(modelData)
                readonly property bool selected: isSession ? modelData.sessionPath === root.activeSessionPath
                                                          : isFolder && root.samePath(modelData.path, root.workspacePath)
                width: sessionList.width
                height: matches ? (isSession ? 62 : isFolder ? 52 : 40) : 0
                visible: matches
                clip: true
                radius: 9
                color: selected ? "#e9e4da" : entryMouse.containsMouse ? "#ebe7df" : "transparent"

                MouseArea {
                    id: entryMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !root.switching && (!entry.isSession && !entry.isFolder || root.sessionActionsEnabled)
                    onClicked: {
                        if (entry.isSession)
                            root.sessionSelected(entry.modelData.sessionPath, entry.modelData.path)
                        else if (entry.isFolder)
                            root.workspaceSelected(entry.modelData.path)
                        else
                            root.toggleFolder(entry.modelData.id)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: entry.isSession ? 30 : entry.isFolder ? 12 : 0
                    anchors.rightMargin: 4
                    spacing: 5
                    SidebarButton {
                        visible: !entry.isSession
                        text: root.collapsedFolders[entry.modelData.id] ? "▸" : "▾"
                        onClicked: root.toggleFolder(entry.modelData.id)
                        Accessible.name: "展开或收起 " + entry.modelData.title
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            Layout.fillWidth: true
                            text: entry.modelData.title
                            color: entry.isSession ? "#403d37" : "#786451"
                            font.pixelSize: entry.isSession ? 13 : 12
                            font.bold: entry.selected || !entry.isSession && !entry.isFolder
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: entry.isSession || entry.isFolder
                            text: entry.isFolder ? entry.modelData.path : (entry.modelData.preview || "")
                            color: "#969087"
                            font.pixelSize: 10
                            elide: entry.isFolder ? Text.ElideMiddle : Text.ElideRight
                        }
                    }
                    Label {
                        visible: entry.isSession
                        text: entry.modelData.updatedAt || ""
                        color: "#8e887f"
                        font.pixelSize: 10
                    }
                    SidebarButton {
                        visible: entry.modelData.kind === "project" && !!entry.modelData.managed
                        text: "+"
                        enabled: root.sessionActionsEnabled && !root.switching
                        Accessible.name: "添加工作文件夹"
                        ToolTip.visible: hovered
                        ToolTip.text: "添加工作文件夹"
                        onClicked: {
                            root.menuEntry = entry.modelData
                            folderPicker.open()
                        }
                    }
                    SidebarButton {
                        objectName: "removeEntry_" + entry.modelData.id
                        visible: !entry.isSession && !!entry.modelData.managed
                        text: "×"
                        enabled: !root.switching
                        Accessible.name: "删除 " + entry.modelData.title
                        ToolTip.visible: hovered
                        ToolTip.text: "删除列表项（保留磁盘文件）"
                        onClicked: root.requestRemoval(entry.modelData)
                    }
                    SidebarButton {
                        visible: !entry.isSession && !!entry.modelData.managed
                        text: "···"
                        enabled: !root.switching
                        Accessible.name: "管理 " + entry.modelData.title
                        onClicked: {
                            root.menuEntry = entry.modelData
                            entryMenu.popup()
                        }
                    }
                }
                ToolTip.visible: entryMouse.containsMouse && entry.isFolder
                ToolTip.text: entry.modelData.path || ""
                ToolTip.delay: 700
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 20
                visible: sessionList.count === 0
                text: root.model.loading ? "正在加载历史会话…" : "点击项目旁的 +\n创建项目并添加工作文件夹"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#938e85"
                font.pixelSize: 12
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: 8
            text: (root.switching ? "切换中：" : "工作目录：") + root.workspacePath
            elide: Text.ElideMiddle
            color: "#938e85"
            font.pixelSize: 10
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#e3ded5" }
        SidebarButton {
            Layout.fillWidth: true
            Layout.bottomMargin: 10
            implicitHeight: 44
            text: "⚙   设置"
            onClicked: root.settingsRequested()
        }
    }

    Basic.Menu {
        id: entryMenu
        background: Rectangle { color: "#faf9f6"; border.color: "#e1dcd3"; radius: 8 }
        Basic.MenuItem {
            text: "添加工作文件夹"
            enabled: root.sessionActionsEnabled && !root.switching
            visible: root.menuEntry.kind === "project"
            height: visible ? implicitHeight : 0
            onTriggered: folderPicker.open()
        }
        Basic.MenuItem {
            text: "重命名"
            onTriggered: root.editName(root.menuEntry)
        }
        Basic.MenuItem {
            text: "删除列表项…"
            onTriggered: root.requestRemoval(root.menuEntry)
        }
    }

    Dialogs.FolderDialog {
        id: folderPicker
        title: "添加工作文件夹并切换到此目录"
        onAccepted: root.workspaceAdditionRequested(root.menuEntry.id, selectedFolder.toString())
    }

    Basic.Dialog {
        id: nameDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 340
        title: root.menuEntry.id ? "重命名" : "添加项目"
        background: Rectangle { color: "#faf9f6"; radius: 10; border.color: "#e1dcd3" }
        contentItem: ColumnLayout {
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "自定义显示名称"
                color: "#403d37"
                selectByMouse: true
                background: Rectangle { color: "#ffffff"; radius: 6; border.color: "#e1dcd3" }
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.projects ? root.projects.error : ""
                color: "#a95137"
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                SidebarButton { text: "取消"; implicitWidth: 64; onClicked: nameDialog.close() }
                SidebarButton {
                    text: "保存"
                    implicitWidth: 64
                    enabled: nameField.text.trim().length > 0
                    onClicked: {
                        let ok = root.menuEntry.kind === "project"
                            ? root.projects.saveProject(root.menuEntry.id, nameField.text)
                            : root.projects.renameFolder(root.menuEntry.id, nameField.text)
                        if (ok)
                            nameDialog.close()
                    }
                }
            }
        }
    }

    Basic.Dialog {
        id: removeDialog
        objectName: "projectRemovalDialog"
        anchors.centerIn: Overlay.overlay
        width: 340
        modal: true
        closePolicy: Popup.NoAutoClose
        title: root.removalEntry.kind === "project" ? "确认删除项目？" : "确认删除工作文件夹？"
        background: Rectangle { color: "#faf9f6"; radius: 10; border.color: "#e1dcd3" }
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: "将删除列表项：" + (root.removalEntry.title || "")
                wrapMode: Text.WrapAnywhere
                color: "#625e56"
            }
            Label {
                Layout.fillWidth: true
                text: "仅移除项目栏配置，不删除磁盘文件或会话，不改变当前工作目录。删除项目会同时移除其工作文件夹关联；历史会话仍可在未分组中找到。"
                wrapMode: Text.Wrap
                color: "#625e56"
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.projects ? root.projects.error : ""
                wrapMode: Text.Wrap
                color: "#a95137"
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "cancelProjectRemoval"
                text: "取消"
                onClicked: {
                    removeDialog.close()
                    console.info("[Projects] 用户取消删除；id=" + root.removalEntry.id)
                }
            }
            Button {
                objectName: "confirmProjectRemoval"
                text: "确认删除"
                enabled: !!root.projects && !root.switching
                onClicked: {
                    if (root.projects.removeEntry(root.removalEntry.id)) {
                        console.info("[Projects] 删除列表项成功；id=" + root.removalEntry.id + "; diskDeleted=false")
                        removeDialog.close()
                    }
                }
            }
        }
    }
}
