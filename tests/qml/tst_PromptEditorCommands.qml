import QtQuick
import QtTest
import "../../qml/components"

/** 验证 PromptEditor 的命令补全、附件芯片与选区主题配色。 */
TestCase {
    id: testCase
    name: "PromptEditorCommands"
    visible: true
    width: 720
    height: 260
    when: windowShown

    PromptEditor {
        id: prompt
        anchors.fill: parent
        connected: true
        busy: false
        commands: [
            { name: "review", invocation: "/review", description: "Review changes", source: "prompt" },
            { name: "skill:brave-search", invocation: "/skill:brave-search", description: "Search", source: "skill" }
        ]
    }
    SignalSpy { id: removedSpy; target: prompt; signalName: "attachmentRemoved" }

    /** 每个用例前恢复浅色主题，避免单例状态在用例间泄漏。 */
    function init() {
        Theme.dark = false
    }

    /** 清理输入框焦点，避免后续 QML TestCase 继承编辑器焦点。 */
    function cleanupTestCase() {
        prompt.visible = false
        testCase.forceActiveFocus()
    }

    /** 首次加载及主题往返切换时，选中文字始终使用主题定义的高对比度颜色。 */
    function test_selectionColorsFollowTheme() {
        const editor = findChild(prompt, "promptTextArea")
        verify(editor !== null)
        compare(editor.selectionColor, Theme.textSelectionBg)
        compare(editor.selectedTextColor, Theme.textSelectionFg)

        Theme.dark = true
        tryCompare(editor, "selectionColor", Theme.textSelectionBg)
        compare(editor.selectedTextColor, Theme.textSelectionFg)

        Theme.dark = false
        tryCompare(editor, "selectionColor", Theme.textSelectionBg)
        compare(editor.selectedTextColor, Theme.textSelectionFg)
    }

    /** `/` 打开补全，上下键移动，Enter 插入命令且不提交。 */
    function test_commandAutocomplete() {
        mouseClick(prompt, 60, 25)
        keyClick(Qt.Key_Slash)
        compare(prompt.text, "/")
        verify(prompt.commandPopupVisible)
        compare(prompt.filteredCommands.length, 2)
        keyClick(Qt.Key_Down)
        compare(prompt.commandIndex, 1)
        keyClick(Qt.Key_Up)
        compare(prompt.commandIndex, 0)
        keyClick(Qt.Key_Return)
        compare(prompt.text, "/review ")
        verify(!prompt.commandPopupVisible)
    }

    /** 输入前缀后只保留匹配项，空格结束补全。 */
    function test_prefixFilter() {
        prompt.text = ""
        mouseClick(prompt, 60, 25)
        keyClick(Qt.Key_Slash)
        keyClick(Qt.Key_S)
        compare(prompt.filteredCommands.length, 1)
        compare(prompt.filteredCommands[0].name, "skill:brave-search")
        keyClick(Qt.Key_Space)
        verify(!prompt.commandPopupVisible)
    }

    /** 拖放文件路径按行插入输入框，重复路径只保留一份。 */
    function test_droppedPathsAppendToPrompt() {
        prompt.text = "请检查"
        const editor = findChild(prompt, "promptTextArea")
        editor.cursorPosition = prompt.text.length
        const paths = prompt.insertDroppedPaths(["E:/workspace/readme.md", "E:/workspace/readme.md",
                                                 "E:/workspace/main.cpp"])
        compare(paths.length, 2)
        compare(prompt.text, "请检查\nE:/workspace/readme.md\nE:/workspace/main.cpp")
        verify(editor.cursorPosition > "请检查".length)
        prompt.text = ""
    }

    /** 附件芯片随列表变化，移除回调带出正确下标。 */
    function test_attachmentChips() {
        prompt.attachments = [
            { name: "a.txt", image: false },
            { name: "b.png", image: true }
        ]
        compare(prompt.attachmentItems.length, 2)
        prompt.attachmentRemoved(1)
        compare(removedSpy.count, 1)
        compare(removedSpy.signalArguments[0][0], 1)
        prompt.attachments = []
        compare(prompt.attachmentItems.length, 0)
    }
}
