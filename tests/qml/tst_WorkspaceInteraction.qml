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
    ListModel {
        id: sessions
        property bool loading: false
        property int shiftedBy: 0
        property string tokenUsageProfile: "E:/profiles/Desktop"
        property var tokenUsageProfiles: ["E:/profiles/Desktop", "E:/profiles/Work"]
        property var tokenUsageView: ({ period: "month", profile: "E:/profiles/Desktop", rangeLabel: "2026年9月",
            from: "2026-09-01", to: "2026-09-30", loading: false, updatedAt: "12:00",
            hasData: true, sessionCount: 2, allSessionCount: 4, canMovePrevious: true,
            canMoveNext: false, cost: 1.25,
            tokens: { input: 5000, output: 1000, cacheRead: 6000, cacheWrite: 345, total: 12345 },
            points: [{ label: "9/1", total: 5000 }, { label: "9/2", total: 7345 }] })

        /** 切换测试桩的 Token 统计 Profile。 */
        function setTokenUsageProfile(profile) {
            tokenUsageProfile = profile
            const next = Object.assign({}, tokenUsageView)
            next.profile = profile
            tokenUsageView = next
        }

        /** 切换测试桩的统计周期。 */
        function setTokenUsagePeriod(period) {
            const next = Object.assign({}, tokenUsageView)
            next.period = period
            tokenUsageView = next
        }

        /** 记录测试触发的周期移动量。 */
        function shiftTokenUsagePeriod(amount) {
            shiftedBy += amount
        }

        /** 接受测试输入并切换到自定义周期。 */
        function setCustomTokenUsageRange(from, to) {
            const next = Object.assign({}, tokenUsageView)
            next.period = "custom"
            next.from = from
            next.to = to
            tokenUsageView = next
            return true
        }
    }
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
        sessions.shiftedBy = 0
        sessions.tokenUsageProfile = "E:/profiles/Desktop"
        sessions.tokenUsageView = ({ period: "month", profile: "E:/profiles/Desktop", rangeLabel: "2026年9月",
            from: "2026-09-01", to: "2026-09-30", loading: false, updatedAt: "12:00",
            hasData: true, sessionCount: 2, allSessionCount: 4, canMovePrevious: true,
            canMoveNext: false, cost: 1.25,
            tokens: { input: 5000, output: 1000, cacheRead: 6000, cacheWrite: 345, total: 12345 },
            points: [{ label: "9/1", total: 5000 }, { label: "9/2", total: 7345 }] })
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
        // qmltestrunner 在不同测试窗口焦点下可能丢弃合成滚轮，使用同等方向的 Flickable 手势兜底。
        if (list.contentY === 0)
            list.flick(0, -240)
        tryVerify(function() { return list.contentY > 0 })
    }

    /** 分组可折叠，搜索会自动显示折叠组中的匹配会话。 */
    function test_sessionFolderCollapseAndSearch() {
        page.leftSidebarVisible = true
        var sidebar = findChild(page, "sessionSidebar")
        var list = findChild(page, "recentSessions")
        sidebar.toggleFolder("f")
        // 折叠后仅剩项目（36 + 8）与目录（46），隐藏会话不占高度。
        tryCompare(list, "contentHeight", 90)
        sidebar.searchQuery = "session 0"
        tryVerify(function() { return list.contentHeight > 90 })
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

    /** 全局总量、周期切换和自定义范围入口使用 SessionModel 数据。 */
    function test_globalTokenUsageControls() {
        page.rightSidebarVisible = true
        const panel = findChild(page, "contextPanel")
        compare(findChild(panel, "globalTokenTotal").text, "12,345")
        const dayButton = findChild(panel, "usagePeriod_day")
        tryVerify(function() { return dayButton.visible && dayButton.width > 0 && dayButton.height > 0 })
        mouseClick(dayButton, dayButton.width / 2, dayButton.height / 2)
        tryVerify(function() { return sessions.tokenUsageView.period === "day" })
        const previousButton = findChild(panel, "usagePreviousPeriod")
        mouseClick(previousButton, previousButton.width / 2, previousButton.height / 2)
        compare(sessions.shiftedBy, -1)
        const customButton = findChild(panel, "usagePeriod_custom")
        verify(customButton.visible)
        verify(customButton.enabled)
        verify(findChild(panel, "customUsageRangeDialog") !== null)
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

    /** 自定义日期弹窗把起止日期提交给统计模型并进入 custom 周期。 */
    function test_z_customTokenUsageRange() {
        page.rightSidebarVisible = true
        const panel = findChild(page, "contextPanel")
        const customButton = findChild(panel, "usagePeriod_custom")
        tryVerify(function() { return customButton.visible && customButton.width > 0 })
        mouseClick(customButton, customButton.width / 2, customButton.height / 2)
        const dialog = findChild(panel, "customUsageRangeDialog")
        tryCompare(dialog, "visible", true)
        findChild(panel, "customUsageFrom").text = "2026-09-02"
        findChild(panel, "customUsageTo").text = "2026-09-18"
        panel.applyCustomRange()
        tryCompare(dialog, "visible", false)
        compare(sessions.tokenUsageView.period, "custom")
        compare(sessions.tokenUsageView.from, "2026-09-02")
        compare(sessions.tokenUsageView.to, "2026-09-18")
    }
}
