import QtQuick
import QtTest
import "../../qml/components"

/** 验证工作期间输入、引导、后续、取回及中止快捷键。 */
TestCase {
    id: testCase
    name: "PromptQueue"
    visible: true
    width: 720
    height: 220
    when: windowShown

    PromptEditor {
        id: prompt
        anchors.fill: parent
        connected: true
        busy: true
    }
    SignalSpy { id: submitted; target: prompt; signalName: "submit" }
    SignalSpy { id: aborted; target: prompt; signalName: "abortRequested" }
    SignalSpy { id: retrieved; target: prompt; signalName: "retrieveRequested" }

    /** 忙碌不锁定编辑器，Enter 与 Alt+Enter 使用不同投递语义。 */
    function test_busyInputAndShortcuts() {
        mouseClick(prompt, 40, 25)
        keyClick(Qt.Key_A)
        compare(prompt.text, "a")
        keyClick(Qt.Key_Return)
        compare(submitted.count, 1)
        compare(submitted.signalArguments[0][1], false)
        keyClick(Qt.Key_Return, Qt.AltModifier)
        compare(submitted.count, 2)
        compare(submitted.signalArguments[1][1], true)
        keyClick(Qt.Key_Up, Qt.AltModifier)
        compare(retrieved.count, 1)
        keyClick(Qt.Key_Escape)
        compare(aborted.count, 1)
        keyClick(Qt.Key_Return, Qt.ShiftModifier)
        compare(submitted.count, 2)
        verify(prompt.text.includes("\n"))
    }
}
