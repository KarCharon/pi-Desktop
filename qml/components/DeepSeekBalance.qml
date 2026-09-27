import QtQuick
import QtQuick.Controls

/** 余额刷新按钮及非交互扣减浮字；字号相同、不缩放、不改变状态栏布局。 */
Item {
    id: root
    /** 后端余额服务或测试桩，仅接收格式化数据。 */
    required property var controller
    /** 上层按后端枚举传入错误/账户不可用配色。 */
    property bool warning: false
    /** 浮字与余额共用字号，不进行放大动画。 */
    property int amountFontSize: 10
    /** 当前浮字的有界列表，最多三条。 */
    property var particles: []
    /** 非交互覆盖层由窗口注入，避免 footer 裁剪并保持在弹窗下方。 */
    property Item floatLayer: null
    implicitWidth: Math.min(320, button.implicitWidth)
    implicitHeight: 28
    visible: controller ? controller.enabled : false

    /** 仅上报白名单动画阶段和数量，不输出金额之外的用户数据。 */
    function trace(stage) {
        if (controller && typeof controller.traceAnimation === "function")
            controller.traceAnimation(stage, particles.length)
    }

    /** 停止并销毁所有旧账户浮字，避免异步动画串用。 */
    function clearParticles() {
        trace("cleared")
        for (let item of particles)
            item.destroy()
        particles = []
    }

    /** 仅显示精确差额，不在 QML 中执行金额算术；超限移除最旧浮字。 */
    function showDecrease(currency, amount) {
        trace("received")
        if (!visible || !floatLayer) {
            trace(!visible ? "skip_hidden" : "skip_no_layer")
            return
        }
        let list = particles.slice()
        if (list.length >= 3) {
            trace("capacity_drop")
            list.shift().destroy()
        }
        const symbol = currency === "CNY" ? "¥" : currency === "USD" ? "$" : currency + " "
        const point = root.mapToItem(floatLayer, 0, 0)
        const item = particle.createObject(floatLayer, {
            "text": "💸 -" + symbol + amount,
            "x": Math.max(0, Math.min(point.x, floatLayer.width - 150)),
            "y": point.y - 20 - list.length * 16
        })
        if (item) {
            list.push(item)
            particles = list
            trace("created")
        } else {
            trace("create_failed")
        }
    }

    /** 自然播放完成时从跟踪列表释放对象，保持队列有界。 */
    function removeParticle(item) {
        trace("finished")
        particles = particles.filter(entry => entry !== item)
        item.destroy()
    }

    Component.onDestruction: clearParticles()
    onVisibleChanged: { if (!visible) clearParticles() }

    ToolButton {
        id: button
        anchors.fill: parent
        padding: 4
        text: root.controller ? root.controller.displayText : ""
        font.pixelSize: root.amountFontSize
        Accessible.name: text + "，点击刷新"
        contentItem: Text {
            text: button.text
            font: button.font
            color: root.warning ? Theme.danger : Theme.textSecondary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        ToolTip.visible: hovered
        ToolTip.text: root.controller ? root.controller.statusText
            + (root.controller.diagnosticLogPath ? "\n诊断日志：" + root.controller.diagnosticLogPath : "") : ""
        onClicked: root.controller.refresh()
    }
    Connections {
        target: root.controller
        /** 账户或启用上下文变化后不能继续播放旧浮字。 */
        function onContextReset() { root.clearParticles() }
        /** currency 是币种代码，amount 是 C++ 精确计算的正差额字符串。 */
        function onBalanceDecreased(currency, amount) { root.showDecrease(currency, amount) }
    }
    Component {
        id: particle
        Text {
            id: floating
            font.pixelSize: root.amountFontSize
            font.bold: true
            color: Theme.danger
            opacity: 1
            z: 1
            // Text 无输入处理器，不拦截聊天或弹窗点击。
            ParallelAnimation {
                running: true
                NumberAnimation { target: floating; property: "y"; to: floating.y - 64; duration: 5000; easing.type: Easing.OutQuad }
                SequentialAnimation {
                    // 前两秒保持清晰，后三秒渐隐，便于用户看清差额。
                    PauseAnimation { duration: 2000 }
                    NumberAnimation { target: floating; property: "opacity"; to: 0; duration: 3000 }
                }
                onFinished: root.removeParticle(floating)
            }
        }
    }
}
