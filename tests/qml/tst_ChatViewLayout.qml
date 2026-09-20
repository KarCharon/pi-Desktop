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
}
