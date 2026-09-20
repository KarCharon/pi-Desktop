import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/components"

/**
 * 验证滚动条的状态尺寸、拖动行为和按需显示，防止原生样式再次介入。
 */
TestCase {
    id: testCase
    name: "WorkspaceScrollBar"
    visible: true
    width: 240
    height: 320
    when: windowShown

    Flickable {
        id: flick
        width: 200
        height: 300
        contentHeight: 1200
        contentWidth: width
        ScrollBar.vertical: WorkspaceScrollBar { id: bar }
    }

    /** 重置滚动位置和内容高度，隔离各用例的交互状态。 */
    function init() {
        flick.contentHeight = 1200
        flick.contentY = 0
        mouseMove(testCase, 220, 310)
        bar.active = false
    }

    /** 静止、活动、悬停和拖动时滑块均为六像素，且拖动能改变内容位置。 */
    function test_stableThumbDuringInteraction() {
        compare(bar.width, 12)
        compare(bar.contentItem.width, 6)
        bar.active = true
        compare(bar.contentItem.width, 6)
        bar.active = false
        var thumbY = bar.contentItem.y + bar.contentItem.height / 2
        mouseMove(bar, 6, thumbY)
        tryCompare(bar, "hovered", true)
        compare(bar.contentItem.width, 6)
        mousePress(bar, 6, thumbY)
        compare(bar.pressed, true)
        mouseMove(bar, 6, thumbY + 60)
        compare(bar.contentItem.width, 6)
        verify(flick.contentY > 0)
        mouseRelease(bar, 6, thumbY + 60)
        mouseMove(testCase, 220, 310)
        bar.active = false
        compare(bar.contentItem.width, 6)
        compare(bar.background.color, Qt.color("transparent"))
    }

    /** 内容不足一屏时隐藏滚动条，超出一屏后恢复显示。 */
    function test_asNeeded() {
        flick.contentHeight = 100
        tryCompare(bar, "visible", false)
        flick.contentHeight = 1200
        tryCompare(bar, "visible", true)
    }
}
