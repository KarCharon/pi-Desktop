import QtQuick
import QtTest
import "../../qml/components"

/** 验证 Token 统计 Profile 下拉框只切换统计范围，不改变其他会话状态。 */
TestCase {
    id: testCase
    name: "TokenUsageProfile"
    visible: true
    width: 360
    height: 720
    when: windowShown

    QtObject {
        id: usageModel
        property string tokenUsageProfile: "E:/profiles/Desktop"
        property var tokenUsageProfiles: ["E:/profiles/Desktop", "E:/profiles/Work"]
        property var tokenUsageView: ({ period: "month", profile: tokenUsageProfile,
            rangeLabel: "2026年9月", loading: false, hasData: true, allSessionCount: 2,
            sessionCount: 1, cost: 0.5,
            tokens: { input: 100, output: 20, cacheRead: 0, cacheWrite: 0, total: 120 },
            points: [{ label: "9/1", total: 120 }] })

        /** 切换测试桩中的统计 Profile。 */
        function setTokenUsageProfile(profile) {
            tokenUsageProfile = profile
            tokenUsageView = Object.assign({}, tokenUsageView, { profile: profile })
        }

        /** 提供统计面板所需的周期接口。 */
        function setTokenUsagePeriod(period) {
            tokenUsageView = Object.assign({}, tokenUsageView, { period: period })
        }

        /** 提供统计面板所需的固定周期移动接口。 */
        function shiftTokenUsagePeriod(amount) {}

        /** 提供统计面板所需的自定义范围接口。 */
        function setCustomTokenUsageRange(from, to) {
            tokenUsageView = Object.assign({}, tokenUsageView,
                                           { period: "custom", from: from, to: to })
            return true
        }
    }

    ContextPanel {
        id: panel
        anchors.fill: parent
        usageModel: usageModel
        sessionStats: ({})
        workspacePath: "E:/workspace"
        sessionName: "Test"
    }

    /** 关闭测试控件，避免下一个 QML TestCase 继承下拉框焦点。 */
    function cleanupTestCase() {
        const selector = findChild(panel, "usageProfileSelector")
        if (selector && selector.popup.visible)
            selector.popup.close()
        panel.visible = false
        testCase.forceActiveFocus()
    }

    /** 下拉框展示候选 Profile，并将选择转发给统计模型。 */
    function test_switchesUsageProfile() {
        const selector = findChild(panel, "usageProfileSelector")
        tryVerify(function() { return selector.visible && selector.width > 0 })
        compare(selector.count, 2)
        panel.chooseUsageProfile(usageModel.tokenUsageProfiles[1])
        compare(usageModel.tokenUsageProfile, "E:/profiles/Work")
        compare(usageModel.tokenUsageView.profile, "E:/profiles/Work")
        tryCompare(selector, "currentText", "E:/profiles/Work")
    }
}
