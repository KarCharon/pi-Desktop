pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

/**
 * 显示工作提示和依次跳动的三点；动画仅表示本地界面活动，不代表服务端心跳。
 */
Item {
    id: root
    property string text: "Thinking"
    property bool running: false
    property color color: "#9b958b"
    property int fontPixelSize: 11
    implicitWidth: caption.implicitWidth + dots.width
    implicitHeight: Math.max(20, caption.implicitHeight)

    Label {
        id: caption
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(implicitWidth, root.width - dots.width))
        text: root.text
        color: root.color
        font.pixelSize: root.fontPixelSize
        elide: Text.ElideRight
    }

    Item {
        id: dots
        anchors.left: caption.right
        width: root.running ? 26 : 0
        height: root.height
        visible: root.running

        Repeater {
            model: 3
            delegate: Rectangle {
                id: dot
                required property int index
                objectName: "workingDot" + index
                readonly property real restY: dots.height / 2 + 2
                x: 5 + index * 7
                y: restY
                width: 3
                height: 3
                radius: 1.5
                color: root.color

                // 错开起跳时间；隐藏或结束时自动停止，不用 JS 高频定时器驱动。
                SequentialAnimation on y {
                    running: root.running && root.visible
                    loops: Animation.Infinite
                    PauseAnimation { duration: dot.index * 140 }
                    NumberAnimation { from: dot.restY; to: dot.restY - 5; duration: 180; easing.type: Easing.OutQuad }
                    NumberAnimation { from: dot.restY - 5; to: dot.restY; duration: 180; easing.type: Easing.InQuad }
                    PauseAnimation { duration: 640 - dot.index * 140 }
                }
            }
        }
    }
}
