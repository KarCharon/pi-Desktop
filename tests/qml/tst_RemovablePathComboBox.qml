import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/components"

/** 验证下拉项的独立删除按钮、确认取消和删除失败重试不会意外选择或删除路径。 */
TestCase {
    id: testCase
    name: "RemovablePathComboBox"
    visible: true
    width: 640
    height: 480
    when: windowShown

    RemovablePathComboBox {
        id: combo
        width: 500
        optionKind: "Profile 选项"
    }
    SignalSpy { id: removalSpy; target: combo; signalName: "removalConfirmed" }
    SignalSpy { id: selectionSpy; target: combo; signalName: "activated" }

    /** 每次恢复模型及自定义草稿，避免用例间弹窗状态泄漏。 */
    function init() {
        combo.model = ["C:/Profiles/one", "C:/Profiles/two"]
        combo.currentIndex = 0
        combo.editText = "C:/custom-draft"
        removalSpy.clear()
        selectionSpy.clear()
    }

    /** 每次关闭弹窗，避免失败用例影响后续测试。 */
    function cleanup() {
        combo.finishRemoval(true, "")
        combo.popup.close()
    }

    /** 点击行尾叉号只弹确认；取消不删除、不选择，也不覆盖输入草稿。 */
    function test_cancelDoesNotDeleteOrSelect() {
        combo.popup.open()
        tryCompare(combo.popup, "opened", true)
        waitForRendering(combo.popup.contentItem)
        const remove = findChild(combo.popup.contentItem, "removeOption_1")
        verify(remove !== null)
        mouseClick(remove)
        compare(combo.pendingPath, "C:/Profiles/two")
        compare(removalSpy.count, 0)
        compare(selectionSpy.count, 0)
        compare(combo.editText, "C:/custom-draft")
        const dialog = findChild(combo, "optionRemovalDialog")
        verify(dialog !== null)
        tryCompare(dialog, "opened", true)
        waitForRendering(dialog.contentItem)
        mouseClick(findChild(dialog, "cancelOptionRemoval"))
        tryCompare(dialog, "visible", false)
        compare(removalSpy.count, 0)
        compare(combo.count, 2)
    }

    /** 使用路径快照确认，失败时保留对话框，成功后才关闭。 */
    function test_confirmUsesSnapshotAndFailureStaysOpen() {
        combo.requestRemoval("C:/Profiles/two")
        const dialog = findChild(combo, "optionRemovalDialog")
        tryCompare(dialog, "opened", true)
        combo.model = ["C:/Profiles/changed"]
        mouseClick(findChild(dialog, "confirmOptionRemoval"))
        compare(removalSpy.count, 1)
        compare(removalSpy.signalArguments[0][0], "C:/Profiles/two")
        combo.finishRemoval(false, "无法写入配置")
        verify(dialog.visible)
        compare(combo.removalError, "无法写入配置")
        combo.finishRemoval(true, "")
        tryCompare(dialog, "visible", false)
    }

    /** 点击普通选项仍可选择，新增删除按钮不破坏下拉框原有行为。 */
    function test_regularSelectionStillWorks() {
        combo.popup.open()
        tryCompare(combo.popup, "opened", true)
        waitForRendering(combo.popup.contentItem)
        const remove = findChild(combo.popup.contentItem, "removeOption_1")
        verify(remove !== null)
        mouseClick(remove.parent, 30, 15)
        compare(combo.currentIndex, 1)
        compare(combo.editText, "C:/Profiles/two")
        compare(selectionSpy.count, 1)
        compare(removalSpy.count, 0)
    }
}
