import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/components"

/** 使用纯 QML 桩验证浮字字号、上浮、渐隐、容量和账户切换清理。 */
TestCase {
    id: testCase
    name: "DeepSeekBalance"
    width: 600
    height: 300
    visible: true
    when: windowShown

    QtObject {
        id: backend
        /** 模拟已启用的官方模型。 */
        property bool enabled: true
        /** 模拟主栏格式化余额。 */
        property string displayText: "DeepSeek: 💵 ¥109.98"
        /** 模拟脱敏悬浮提示。 */
        property string statusText: "官方账户余额"
        /** 测试桩不写入真实磁盘日志。 */
        property string diagnosticLogPath: ""
        /** 记录白名单动画阶段以验证诊断链。 */
        property var stages: []
        /** 仅收集动画阶段，不记录展示文本。 */
        function traceAnimation(stage, count) { stages = stages.concat([stage]) }
        /** 模拟刷新调用次数。 */
        property int refreshCount: 0
        /** currency 为币种，amount 为精确正差额。 */
        signal balanceDecreased(string currency, string amount)
        /** 通知旧账户已失效。 */
        signal contextReset()
        /** 记录按钮调用，不访问网络。 */
        function refresh() { refreshCount++ }
    }
    Item {
        id: layer
        anchors.fill: parent
    }
    DeepSeekBalance {
        id: balance
        x: 200
        y: 240
        controller: backend
        floatLayer: layer
    }

    /** 每个用例清空浮字并恢复启用状态。 */
    function init() {
        balance.clearParticles()
        backend.enabled = true
        backend.stages = []
    }

    /** 红字不放大、不改变 footer 高度，播放完成释放。 */
    function test_animation() {
        backend.balanceDecreased("CNY", "0.02")
        compare(balance.particles.length, 1)
        const item = balance.particles[0]
        compare(item.text, "💸 -¥0.02")
        compare(item.font.pixelSize, balance.amountFontSize)
        const start = item.y
        wait(500)
        verify(item.y < start)
        compare(item.opacity, 1)
        compare(balance.implicitHeight, 28)
        wait(3000)
        compare(balance.particles.length, 1)
        verify(item.y < start - 32)
        verify(item.opacity > 0 && item.opacity < 1)
        tryCompare(balance, "particles", [], 2500)
        compare(backend.stages, ["received", "created", "finished"])
    }

    /** 突发事件最多保留三条；上下文改变立即清除。 */
    function test_boundedAndReset() {
        for (let i = 0; i < 5; ++i)
            backend.balanceDecreased("CNY", "0.01")
        compare(balance.particles.length, 3)
        verify(backend.stages.indexOf("capacity_drop") >= 0)
        backend.contextReset()
        compare(balance.particles.length, 0)
        backend.enabled = false
        backend.balanceDecreased("CNY", "0.01")
        compare(balance.particles.length, 0)
        verify(backend.stages.indexOf("cleared") >= 0)
        verify(backend.stages.indexOf("skip_hidden") >= 0)
    }
}
