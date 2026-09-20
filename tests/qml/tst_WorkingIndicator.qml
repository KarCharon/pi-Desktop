import QtQuick
import QtTest
import "../../qml/components"

/** 验证三点动画可见时运行，结束和隐藏后停止，提示切换不改变布局高度。 */
TestCase {
    id: testCase
    name: "WorkingIndicator"
    visible: true
    width: 300
    height: 100
    when: windowShown

    WorkingIndicator {
        id: indicator
        width: 250
        text: "Thinking"
    }

    /** 每个测试前恢复组件初始状态。 */
    function init() {
        indicator.running = false
        indicator.visible = true
        indicator.text = "Thinking"
    }

    /** 三点错峰跳动，停止后位置不再更新。 */
    function test_animationLifecycle() {
        var dot = findChild(indicator, "workingDot0")
        var second = findChild(indicator, "workingDot1")
        indicator.running = true
        tryVerify(function() { return dot.y < dot.restY - 1 && Math.abs(dot.y - second.y) > 0.1 })
        var height = indicator.height
        indicator.text = "Contemplating"
        compare(indicator.height, height)
        indicator.running = false
        wait(30)
        var stoppedY = dot.y
        wait(150)
        compare(dot.y, stoppedY)
        compare(dot.visible, false)
        indicator.running = true
        tryVerify(function() { return dot.y < dot.restY - 1 })
        indicator.visible = false
        wait(30)
        var hiddenY = dot.y
        wait(150)
        compare(dot.y, hiddenY)
    }
}
