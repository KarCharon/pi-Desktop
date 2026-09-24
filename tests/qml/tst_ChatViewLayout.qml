import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml/components"

/**
 * 验证各类消息铺满中间栏，并在分栏缩放后保留固定边距而非固定最大宽度。
 */
TestCase {
    id: testCase
    name: "ChatViewLayout"
    visible: true
    width: 1800
    height: 600
    when: windowShown

    ListModel {
        id: messages
        signal contentUpdated()
    }

    ChatView {
        id: chat
        width: 1600
        height: 560
        model: messages
    }

    /** 监视消息文本变化，确认缩放会从源文本重新导入 Markdown。 */
    SignalSpy { id: textSpy }

    /** 提供用户、助手、系统消息及工具调用四种委托类型。 */
    function test_fillWidth_data() {
        return [
            { tag: "assistant", entryType: "message", role: "assistant" },
            { tag: "user", entryType: "message", role: "user" },
            { tag: "system", entryType: "message", role: "system" },
            { tag: "tool", entryType: "tool", role: "assistant" }
        ]
    }

    /** 校验实际加载内容的宽度、左右边距及行高，覆盖宽屏和缩窄后的布局。 */
    function verifyMessageWidth(expectedWidth) {
        chat.forceLayout()
        tryVerify(function() { return chat.itemAtIndex(0) !== null })
        var row = chat.itemAtIndex(0)
        var loader = row.children[0]
        tryVerify(function() { return loader.item !== null })
        tryCompare(row, "width", expectedWidth)
        tryCompare(loader.item, "width", expectedWidth - 52)
        compare(loader.x, 26)
        compare(row.x, 0)
        verify(row.height > 0)
        compare(row.height, loader.height)
    }

    /** 用户滚轮上翻后，后续流式更新不应将视图强行拉回末尾。 */
    function test_historyScrollIsNotStolen() {
        messages.clear()
        for (var i = 0; i < 40; ++i)
            messages.append({ entryType: "message", messageRole: "assistant",
                content: "Message " + i, itemState: "completed", toolName: "", toolInput: "",
                toolOutput: "", isError: false, timestamp: "12:00" })
        chat.forceLayout()
        chat.positionViewAtEnd()
        wait(100)
        mouseWheel(chat, 100, 100, 0, 120)
        tryCompare(chat, "moving", false)
        tryCompare(chat, "followTail", false)
        var oldY = chat.contentY
        messages.contentUpdated()
        wait(100)
        compare(chat.contentY, oldY)
    }

    /** 中间栏宽度变化时，消息不能停留在旧的八百六十像素上限。 */
    function test_fillWidth(data) {
        messages.clear()
        messages.append({
            entryType: data.entryType, messageRole: data.role,
            content: "用于验证自适应会话宽度的消息。", itemState: "done",
            toolName: "read", toolInput: "example.txt", toolOutput: "ok",
            isError: false, timestamp: "12:00"
        })
        chat.width = 1600
        verifyMessageWidth(1600)
        chat.width = 640
        verifyMessageWidth(640)
        chat.width = 1200
        verifyMessageWidth(1200)
    }

    /** 递归查找消息内的 TextEdit，用于校验字号是否随缩放同步变化。 */
    function findTextEdit(item) {
        if (!item || !item.children)
            return null
        for (var i = 0; i < item.children.length; ++i) {
            var child = item.children[i]
            // TextEdit 有 readOnly 而 Label 没有，据此与头像、状态标签区分。
            if (child.readOnly !== undefined && child.font !== undefined)
                return child
            var found = findTextEdit(child)
            if (found)
                return found
        }
        return null
    }

    /** 恢复默认缩放并等待重新布局，避免单例状态影响其他用例。 */
    function resetZoom() {
        Theme.chatZoom = 1.0
        wait(50)
    }

    /**
     * 对话缩放会同步改变消息正文字号，普通滚轮不缩放。
     *
     * 用 applyZoom 驱动真实缩放逻辑，并校验 Ctrl+滚轮处理器只接受 Ctrl 修饰；
     * 不直接合成带修饰键的滚轮事件，避免 offscreen 平台残留状态污染后续用例。
     */
    function test_zoomAffectsMessageText() {
        resetZoom()
        messages.clear()
        messages.append({ entryType: "message", messageRole: "assistant",
            content: "缩放测试消息", itemState: "completed", toolName: "", toolInput: "",
            toolOutput: "", isError: false, timestamp: "12:00" })
        chat.forceLayout()
        tryVerify(function() { return chat.itemAtIndex(0) !== null })
        var row = chat.itemAtIndex(0)
        var loader = row.children[0]
        tryVerify(function() { return loader.item !== null })
        var textItem = findTextEdit(loader.item)
        verify(textItem !== null)
        compare(textItem.font.pixelSize, 15)

        // Ctrl+滚轮处理器必须只接受 Ctrl，未按 Ctrl 的滚轮仍交给列表滚动。
        var zoomWheel = findChild(chat, "chatZoomWheel")
        verify(zoomWheel !== null)
        compare(zoomWheel.acceptedModifiers, Qt.ControlModifier)

        // 未按 Ctrl 的滚轮不得改变缩放。
        mouseWheel(chat, chat.width / 2, chat.height / 2, 0, 120)
        wait(50)
        compare(Theme.chatZoom, 1.0)

        // 放大后消息正文字号同步增大。
        chat.applyZoom(120)
        tryVerify(function() { return Theme.chatZoom > 1.0 })
        tryVerify(function() { return textItem.font.pixelSize === Theme.scaled(15) })

        // 缩小回基准倍率后字号复位。
        chat.applyZoom(-120)
        tryCompare(Theme, "chatZoom", 1.0)
        tryVerify(function() { return textItem.font.pixelSize === 15 })
        resetZoom()
    }

    /** 缩放触发的 Markdown 重解析结束后，消息仍保持 Markdown 渲染且内容不变。 */
    function test_zoomKeepsMarkdownFormat() {
        resetZoom()
        messages.clear()
        messages.append({ entryType: "message", messageRole: "assistant",
            content: "**Bold** 与 `code` 混合", itemState: "completed", toolName: "",
            toolInput: "", toolOutput: "", isError: false, timestamp: "12:00" })
        chat.forceLayout()
        tryVerify(function() { return chat.itemAtIndex(0) !== null })
        var row = chat.itemAtIndex(0)
        var loader = row.children[0]
        tryVerify(function() { return loader.item !== null })
        var textItem = findTextEdit(loader.item)
        compare(textItem.textFormat, TextEdit.MarkdownText)

        // 重解析清空并恢复源文本，但不切换格式，以免累积 Markdown 转义符。
        textSpy.clear()
        textSpy.target = textItem
        textSpy.signalName = "textChanged"

        chat.applyZoom(120)
        tryVerify(function() { return Theme.chatZoom > 1.0 })
        // 重解析为同步完成，缩放后应立刻回到 Markdown 渲染且内容仍在。
        compare(textItem.textFormat, TextEdit.MarkdownText)
        verify(textItem.text.indexOf("code") >= 0)
        verify(textSpy.count >= 2)
        resetZoom()
    }

    /** 多轮放大缩小不得累积 Markdown 转义符，渲染文本与源消息均须保持不变。 */
    function test_zoomDoesNotAccumulateEscapes() {
        resetZoom()
        messages.clear()
        var source = "**路径** `F:\\Application\\PiDesktop\\bin` 与 /api/chat\n\n"
                   + "普通路径 C:\\Users\\test，转义 \\*星号\\* 和 a_b。\n\n"
                   + "```text\nF:\\Application\\bin /api/chat\n```"
        messages.append({ entryType: "message", messageRole: "assistant",
            content: source, itemState: "completed", toolName: "", toolInput: "",
            toolOutput: "", isError: false, timestamp: "12:00" })
        chat.forceLayout()
        tryVerify(function() { return chat.itemAtIndex(0) !== null })
        var loader = chat.itemAtIndex(0).children[0]
        tryVerify(function() { return loader.item !== null })
        var textItem = findTextEdit(loader.item)
        var originalPlainText = textItem.getText(0, textItem.length)
        var originalMarkdown = textItem.text
        for (var i = 0; i < 5; ++i) {
            chat.applyZoom(120)
            chat.applyZoom(-120)
            compare(textItem.getText(0, textItem.length), originalPlainText)
            compare(textItem.text, originalMarkdown)
            compare(loader.item.messageText, source)
        }
        resetZoom()
    }

    /** 缩放被夹在允许范围内，Ctrl+0 快捷键可恢复默认。 */
    function test_zoomClampAndResetShortcut() {
        resetZoom()
        chat.setZoom(99)
        tryCompare(Theme, "chatZoom", 2.5)
        chat.setZoom(0.01)
        tryCompare(Theme, "chatZoom", 0.7)
        chat.setZoom(1.4)
        tryCompare(Theme, "chatZoom", 1.4)
        keySequence("Ctrl+0")
        tryCompare(Theme, "chatZoom", 1.0)
        resetZoom()
    }
}
