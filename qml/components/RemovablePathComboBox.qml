pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Controls
import QtQuick.Layouts

/** 可编辑的路径下拉框；每项末尾提供删除按钮，确认后才交给后端持久化。 */
Basic.ComboBox {
    id: root
    editable: true
    property string optionKind: "选项"
    property string pendingPath: ""
    property string removalError: ""
    property bool awaitingPopupClose: false
    signal removalConfirmed(string path)

    /** 使用路径快照请求确认，防止列表刷新后误删原索引处的其他选项。 */
    function requestRemoval(path) {
        pendingPath = path
        removalError = ""
        // 等下拉层退出动画结束再显示确认框，避免旧遮罩截获确认/取消点击。
        if (popup.visible) {
            awaitingPopupClose = true
            popup.close()
        } else {
            confirmation.open()
        }
        console.info("[ConnectionOptions] 请求删除确认；kind=" + optionKind + "; path=" + path)
    }

    /** 后端保存成功才关闭确认框；失败时显示原因并保留重试入口。 */
    function finishRemoval(success, error) {
        if (success)
            confirmation.close()
        else
            removalError = error || "删除失败，请重试。"
    }

    Connections {
        target: root.popup
        /** 下拉层完全关闭后再开启确认层，保持唯一的模态交互目标。 */
        function onClosed() {
            if (root.awaitingPopupClose) {
                root.awaitingPopupClose = false
                confirmation.open()
            }
        }
    }

    delegate: Basic.ItemDelegate {
        id: option
        required property int index
        required property string modelData
        width: root.width
        height: 42
        highlighted: root.highlightedIndex === index
        contentItem: RowLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: option.modelData
                elide: Text.ElideMiddle
                ToolTip.visible: option.hovered
                ToolTip.text: text
            }
            Basic.ToolButton {
                objectName: "removeOption_" + option.index
                text: "×"
                implicitWidth: 30
                implicitHeight: 30
                Accessible.name: "删除 " + option.modelData
                ToolTip.visible: hovered
                ToolTip.text: "从下拉列表删除"
                // 独立按钮消费点击，不触发父项选择，更不立即删除。
                onClicked: root.requestRemoval(option.modelData)
            }
        }
    }

    Basic.Dialog {
        id: confirmation
        objectName: "optionRemovalDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: "确认删除" + root.optionKind + "？"
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: root.pendingPath
                wrapMode: Text.WrapAnywhere
            }
            Label {
                Layout.fillWidth: true
                text: "仅从下拉列表删除，重新扫描也不会再显示；不删除磁盘文件，不改变当前连接。仍可手动输入或浏览使用此路径。"
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: root.removalError
                visible: text.length > 0
                color: Theme.danger
                wrapMode: Text.WordWrap
            }
        }
        footer: DialogButtonBox {
            Basic.Button {
                objectName: "cancelOptionRemoval"
                text: "取消"
                onClicked: {
                    confirmation.close()
                    console.info("[ConnectionOptions] 用户取消删除；kind=" + root.optionKind)
                }
            }
            Basic.Button {
                objectName: "confirmOptionRemoval"
                text: "确认删除"
                onClicked: root.removalConfirmed(root.pendingPath)
            }
        }
    }
}
