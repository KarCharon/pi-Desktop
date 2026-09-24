import QtQuick
import QtTest
import "../../qml/components"

/** 验证项目树折叠、搜索及目录选择与会话选择的独立行为。 */
TestCase {
    id: testCase
    name: "SessionGrouping"
    visible: true
    width: 360
    height: 800
    when: windowShown

    QtObject { id: sessions; property bool loading: false }
    QtObject {
        id: projectFixture
        property var rows: []
        property string error: ""
        property string removedId: ""
        property bool allowRemoval: true
        /** 模拟配置删除的成功与失败，不访问磁盘。 */
        function removeEntry(id) {
            if (!allowRemoval) {
                error = "配置保存失败"
                return false
            }
            removedId = id
            error = ""
            return true
        }
    }
    SessionSidebar {
        id: sidebar
        anchors.fill: parent
        model: sessions
        projects: projectFixture
        workspacePath: "E:/Work/client"
        activeSessionPath: ""
    }
    SignalSpy { id: folderSpy; target: sidebar; signalName: "workspaceSelected" }
    SignalSpy { id: sessionSpy; target: sidebar; signalName: "sessionSelected" }

    /** 每次恢复一个项目和目录，避免不同用例的折叠状态互相影响。 */
    function init() {
        sidebar.collapsedFolders = ({})
        sidebar.searchQuery = ""
        sidebar.switching = false
        sidebar.sessionActionsEnabled = true
        projectFixture.removedId = ""
        projectFixture.error = ""
        projectFixture.allowRemoval = true
        projectFixture.rows = [
            {kind: "project", id: "p", projectId: "p", title: "桌面工具", managed: true, searchText: "桌面工具 client 修复"},
            {kind: "folder", id: "f", projectId: "p", folderId: "f", title: "client", path: "E:/Work/client", managed: true, searchText: "桌面工具 client 修复"},
            {kind: "session", id: "s", projectId: "p", folderId: "f", title: "修复", path: "E:/Work/client", sessionPath: "history.jsonl", preview: "", searchText: "桌面工具 client 修复"}
        ]
        folderSpy.clear()
        sessionSpy.clear()
        wait(50)
    }

    /** 项目和目录叉号都需二次确认，取消或保存失败不会删除目标。 */
    function test_removalRequiresConfirmation() {
        const projectButton = findChild(sidebar, "removeEntry_p")
        verify(projectButton !== null)
        mouseClick(projectButton)
        const dialog = findChild(sidebar, "projectRemovalDialog")
        tryCompare(dialog, "opened", true)
        compare(projectFixture.removedId, "")
        mouseClick(findChild(dialog, "cancelProjectRemoval"))
        tryCompare(dialog, "visible", false)
        compare(projectFixture.removedId, "")
        const folderButton = findChild(sidebar, "removeEntry_f")
        mouseClick(folderButton)
        tryCompare(dialog, "opened", true)
        projectFixture.allowRemoval = false
        mouseClick(findChild(dialog, "confirmProjectRemoval"))
        compare(projectFixture.removedId, "")
        verify(dialog.visible)
        projectFixture.allowRemoval = true
        // 确认期间其他菜单项变化不应改变删除目标。
        sidebar.menuEntry = {id: "p"}
        mouseClick(findChild(dialog, "confirmProjectRemoval"))
        compare(projectFixture.removedId, "f")
        tryCompare(dialog, "visible", false)
        compare(folderSpy.count, 0)
        compare(sidebar.workspacePath, "E:/Work/client")
    }

    /** 折叠隐藏全部子行，零间距不会按会话数量累计空白。 */
    function test_collapsedGroupsHaveNoGaps() {
        const list = findChild(sidebar, "recentSessions")
        sidebar.toggleFolder("p")
        // 项目行包含 36 像素标题与 8 像素组间距，目录和会话分别为 46、58。
        tryCompare(list, "contentHeight", 44)
        compare(list.spacing, 0)
        sidebar.toggleFolder("p")
        tryCompare(list, "contentHeight", 148)
        sidebar.toggleFolder("f")
        tryCompare(list, "contentHeight", 90)
        compare(folderSpy.count, 0)
    }

    /** 搜索自动忽略折叠，清空搜索后恢复用户原来的展开状态。 */
    function test_searchExpandsMatchingAncestors() {
        sidebar.toggleFolder("p")
        sidebar.searchQuery = "修复"
        const list = findChild(sidebar, "recentSessions")
        tryCompare(list, "contentHeight", 148)
        sidebar.searchQuery = "不存在"
        tryCompare(list, "contentHeight", 0)
        sidebar.searchQuery = ""
        tryCompare(list, "contentHeight", 44)
    }

    /** 点击目录传递真实路径；点击历史会话同时传递其工作目录。 */
    function test_clickRoutesWorkspaceAndSession() {
        const list = findChild(sidebar, "recentSessions")
        mouseClick(list, 160, 65)
        compare(folderSpy.count, 1)
        compare(folderSpy.signalArguments[0][0], "E:/Work/client")
        mouseClick(list, 160, 120)
        compare(sessionSpy.count, 1)
        compare(sessionSpy.signalArguments[0][0], "history.jsonl")
        compare(sessionSpy.signalArguments[0][1], "E:/Work/client")
        sidebar.switching = true
        mouseClick(list, 160, 65)
        mouseClick(list, 160, 120)
        compare(folderSpy.count, 1)
        compare(sessionSpy.count, 1)
    }
}
