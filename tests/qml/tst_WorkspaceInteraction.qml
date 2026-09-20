import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml/pages"

/** 验证侧栏折叠、忙碌期滚动和真实上下文数值的显示条件。 */
TestCase {
    id: testCase
    name: "WorkspaceInteraction"
    visible: true
    width: 1400
    height: 800
    when: windowShown

    ListModel { id: messages; signal contentUpdated() }
    ListModel { id: sessions; property bool loading: false }
    QtObject {
        id: agent
        property bool busy: false
        property bool connected: true
        property string sessionFile: ""
        property string sessionName: "Test"
        property string workingText: "Thinking"
        property string statusText: "Thinking…"
        property var sessionStats: ({})
        signal newSessionCreated()
        signal restoreDraftRequested(string text)
        property string queueText: ""
    }
    QtObject {
        id: projectFixture
        property var rows: []
        property string error: ""
    }
    QtObject {
        id: navigationFixture
        property var projects: projectFixture
        property bool workspaceSwitching: false
        property string workspaceError: ""
    }
    ChatPage {
        id: page
        anchors.fill: parent
        chatModel: messages
        sessionModel: sessions
        agent: agent
        workspacePath: "E:/test"
        projectNavigation: navigationFixture
    }

    /** 填充足够长的会话列表，以验证真实滚轮交互。 */
    function initTestCase() {
        let rows = [
            {kind: "project", id: "p", projectId: "p", title: "项目", searchText: "项目 session 0"},
            {kind: "folder", id: "f", folderId: "f", projectId: "p", title: "工作区", path: "E:/test", searchText: "工作区 session 0"}
        ]
        for (var i = 0; i < 40; ++i)
            rows.push({kind: "session", id: "s" + i, projectId: "p", folderId: "f",
                       title: "Session " + i, sessionPath: "session-" + i,
                       path: "E:/test", preview: "preview", updatedAt: "12:00", searchText: "session " + i})
        projectFixture.rows = rows
    }

    /** 重置测试使用的侧栏、忙碌状态和统计数据。 */
    function init() {
        page.leftSidebarVisible = false
        page.rightSidebarVisible = false
        agent.busy = false
        agent.sessionStats = ({})
        const sidebar = findChild(page, "sessionSidebar")
        sidebar.collapsedFolders = ({})
        sidebar.searchQuery = ""
        findChild(page, "recentSessions").contentY = 0
    }

    /** 两侧独立展开，新会话成功信号将两侧一起收起。 */
    function test_toggleAndNewSession() {
        var left = findChild(page, "sessionSidebar")
        var right = findChild(page, "contextPanel")
        compare(left.visible, false)
        compare(right.visible, false)
        mouseClick(findChild(page, "toggleSessions"))
        compare(left.visible, true)
        compare(right.visible, false)
        mouseClick(findChild(page, "toggleContext"))
        compare(right.visible, true)
        agent.newSessionCreated()
        compare(left.visible, false)
        compare(right.visible, false)
    }

    /** 导航按钮保持统一尺寸，展开高亮同步，忙碌时只禁用新建按钮。 */
    function test_navigationButtonStyleAndState() {
        const left = findChild(page, "toggleSessions")
        const right = findChild(page, "toggleContext")
        const create = findChild(page, "newConversation")
        compare(left.height, 34)
        compare(right.height, 34)
        compare(create.height, 34)
        compare(left.selected, false)
        mouseClick(left)
        compare(left.selected, true)
        compare(left.background.radius, 9)
        left.forceActiveFocus(Qt.TabFocusReason)
        verify(left.activeFocus)
        keyClick(Qt.Key_Space)
        compare(left.selected, false)
        agent.busy = true
        compare(create.enabled, false)
        compare(left.enabled, true)
        compare(right.enabled, true)
    }

    /** Agent 忙碌时只禁止会话操作，滚轮仍能向下浏览最近会话。 */
    function test_scrollWhileBusy() {
        page.leftSidebarVisible = true
        agent.busy = true
        var sidebar = findChild(page, "sessionSidebar")
        compare(sidebar.sessionActionsEnabled, false)
        var list = findChild(page, "recentSessions")
        tryVerify(function() { return list.height > 0 && list.contentHeight > list.height })
        compare(list.enabled, true)
        list.contentY = 0
        mouseWheel(list, 80, 100, 0, -120)
        tryVerify(function() { return list.contentY > 0 })
    }

    /** 分组可折叠，搜索会自动显示折叠组中的匹配会话。 */
    function test_sessionFolderCollapseAndSearch() {
        page.leftSidebarVisible = true
        var sidebar = findChild(page, "sessionSidebar")
        var list = findChild(page, "recentSessions")
        sidebar.toggleFolder("f")
        tryCompare(list, "contentHeight", 92)
        sidebar.searchQuery = "session 0"
        tryVerify(function() { return list.contentHeight > 92 })
        sidebar.searchQuery = ""
        sidebar.toggleFolder("f")
    }

    /** 取回队列不覆盖工作期间已经写入的草稿。 */
    function test_restoredQueuePreservesDraft() {
        page.promptText = "draft"
        agent.restoreDraftRequested("queued")
        compare(page.promptText, "queued\n\ndraft")
        page.promptText = ""
    }

    Component {
        id: futureSection
        Rectangle { implicitHeight: 30; color: "transparent" }
    }

    /** Outline 和 Files 默认不创建内容；预留接口可加载未来模块并再次清除。 */
    function test_contextOnlyWithExtensionHooks() {
        page.rightSidebarVisible = true
        var panel = findChild(page, "contextPanel")
        var outline = findChild(panel, "outlineExtension")
        var files = findChild(panel, "filesExtension")
        compare(outline.active, false)
        compare(files.active, false)
        compare(outline.item, null)
        compare(files.item, null)
        panel.outlineContent = futureSection
        panel.filesContent = futureSection
        tryVerify(function() { return outline.item !== null && files.item !== null })
        compare(outline.visible, true)
        compare(files.visible, true)
        panel.outlineContent = null
        panel.filesContent = null
        compare(outline.item, null)
        compare(files.item, null)
        compare(outline.visible, false)
        compare(files.visible, false)
    }

    /** 压缩后的 null 不能显示成零用量；上下文与累计 token 不混淆。 */
    function test_contextUsage() {
        var panel = findChild(page, "contextPanel")
        compare(panel.contextKnown, false)
        agent.sessionStats = { tokens: { total: 105000 }, cost: 0.45,
            contextUsage: { tokens: 60000, contextWindow: 200000, percent: 30 } }
        compare(panel.contextKnown, true)
        compare(panel.tokenUsage.total, 105000)
        compare(panel.contextUsage.tokens, 60000)
        agent.sessionStats = { contextUsage: { tokens: null, contextWindow: 200000, percent: null } }
        compare(panel.contextKnown, false)
    }
}
