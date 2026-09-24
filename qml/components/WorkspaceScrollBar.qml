import QtQuick
import QtQuick.Templates as T

/**
 * 提供不依赖系统主题的工作区滚动条，所有交互状态保持相同滑块宽度。
 */
T.ScrollBar {
    id: control
    policy: T.ScrollBar.AsNeeded
    hoverEnabled: true
    padding: 3
    implicitWidth: 12
    implicitHeight: 12
    minimumSize: 0.04
    visible: policy === T.ScrollBar.AlwaysOn
             || (policy === T.ScrollBar.AsNeeded && size < 1.0)

    // 外层保留十二像素命中区，内层六像素滑块只变色、不随活动状态伸缩。
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? Theme.scrollThumbActive
                               : control.hovered ? Theme.scrollThumbHover : Theme.scrollThumb
    }
    background: Rectangle {
        color: "transparent"
    }
}
