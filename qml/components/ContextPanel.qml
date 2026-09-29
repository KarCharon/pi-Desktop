pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

/**
 * 展示当前 Profile 的全局 Token 趋势、构成及当前会话上下文。
 */
Rectangle {
    id: root

    required property string workspacePath
    required property string sessionName
    property var sessionStats: ({})
    property var usageModel: null
    // 未来模块通过组件注入启用；未提供组件时不创建控件、不占用布局空间。
    property Component outlineContent: null
    property Component filesContent: null

    readonly property var contextUsage: sessionStats.contextUsage || ({})
    readonly property var tokenUsage: sessionStats.tokens || ({})
    readonly property bool contextKnown: typeof contextUsage.tokens === "number"
                                         && typeof contextUsage.percent === "number"
                                         && contextUsage.contextWindow > 0
    readonly property var globalUsage: usageModel && usageModel.tokenUsageView
                                               ? usageModel.tokenUsageView : ({})
    readonly property var globalTokens: globalUsage.tokens || ({})
    readonly property var trendPoints: globalUsage.points || []
    readonly property var profileOptions: usageModel && usageModel.tokenUsageProfiles
                                             ? usageModel.tokenUsageProfiles : []
    readonly property real globalTotal: root.numberOrZero(globalTokens.total)
    readonly property var periodOptions: [
        { label: "日", value: "day" },
        { label: "周", value: "week" },
        { label: "月", value: "month" },
        { label: "总计", value: "total" },
        { label: "自定义", value: "custom" }
    ]

    color: Theme.surface
    border.color: Theme.borderSoft

    Component.onCompleted: console.info("[TokenUsagePanel] initialized; globalUsage="
                                        + (usageModel !== null) + "; period="
                                        + (globalUsage.period || "month")
                                        + "; contextEnabled=true")

    /**
     * 将未知值归零，防止 QML 的 undefined 参与尺寸和比例计算。
     */
    function numberOrZero(value) {
        return typeof value === "number" && isFinite(value) ? value : 0
    }

    /**
     * 使用本地千分位展示主 Token 数值，极大数值改用紧凑单位。
     */
    function formatTokens(value) {
        const number = numberOrZero(value)
        if (number >= 1000000000)
            return (number / 1000000000).toFixed(number >= 100000000000 ? 0 : 2) + "B"
        return Number(number).toLocaleString(Qt.locale(), 'f', 0)
    }

    /**
     * 将分类 Token 压缩为适合窄栏的 K、M、B 单位。
     */
    function formatCompactTokens(value) {
        const number = numberOrZero(value)
        if (number >= 1000000000)
            return (number / 1000000000).toFixed(2) + "B"
        if (number >= 1000000)
            return (number / 1000000).toFixed(2) + "M"
        if (number >= 1000)
            return (number / 1000).toFixed(number >= 100000 ? 0 : 1) + "K"
        return Number(number).toLocaleString(Qt.locale(), 'f', 0)
    }

    /**
     * 计算趋势最大值并至少返回 1，避免空数据产生除零。
     */
    function maximumTrendValue(points) {
        let maximum = 1
        for (let index = 0; index < points.length; ++index)
            maximum = Math.max(maximum, numberOrZero(points[index].total))
        return maximum
    }

    /**
     * 将 Profile 目录压缩为适合下拉框显示的名称。
     */
    function profileLabel(path) {
        const parts = String(path || "").split(/[\\/]/)
        return parts.length && parts[parts.length - 1] ? parts[parts.length - 1] : String(path || "—")
    }

    /**
     * 选择 Token 统计 Profile，不改变当前会话连接。
     */
    function chooseUsageProfile(profile) {
        if (usageModel && typeof usageModel.setTokenUsageProfile === "function")
            usageModel.setTokenUsageProfile(profile)
    }

    /**
     * 切换预定义周期；自定义周期打开日期输入弹窗。
     */
    function choosePeriod(period) {
        if (period === "custom") {
            customFrom.text = globalUsage.from || Qt.formatDate(new Date(), "yyyy-MM-dd")
            customTo.text = globalUsage.to || Qt.formatDate(new Date(), "yyyy-MM-dd")
            customRangeDialog.open()
            return
        }
        if (usageModel && typeof usageModel.setTokenUsagePeriod === "function")
            usageModel.setTokenUsagePeriod(period)
    }

    /**
     * 提交自定义日期范围，后端校验失败时保留弹窗和输入内容。
     */
    function applyCustomRange() {
        if (!usageModel || typeof usageModel.setCustomTokenUsageRange !== "function")
            return
        if (usageModel.setCustomTokenUsageRange(customFrom.text, customTo.text))
            customRangeDialog.close()
    }

    /**
     * 绘制 Token 分类指标行。
     */
    component MetricRow: Item {
        id: metricRoot
        required property color markerColor
        required property string label
        required property var value
        Layout.fillWidth: true
        implicitHeight: 28

        Rectangle {
            x: 1
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: metricRoot.markerColor
        }
        Label {
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            text: metricRoot.label
            color: Theme.textSecondary
            font.pixelSize: 11
        }
        Label {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.formatCompactTokens(metricRoot.value)
            color: Theme.textBody
            font.pixelSize: 11
            font.family: "Cascadia Mono"
            font.weight: Font.DemiBold
        }
    }

    ScrollView {
        id: panelScroll
        anchors.fill: parent
        clip: true
        contentWidth: root.width
        ScrollBar.vertical: WorkspaceScrollBar {
            parent: panelScroll
            x: panelScroll.mirrored ? 0 : panelScroll.width - width
            y: panelScroll.topPadding
            height: panelScroll.availableHeight
        }
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: panelColumn
            width: root.width - 28
            x: 14
            y: 16
            spacing: 0

            Loader {
                objectName: "outlineExtension"
                Layout.fillWidth: true
                active: root.outlineContent !== null
                visible: active
                sourceComponent: root.outlineContent
            }

            Loader {
                objectName: "filesExtension"
                Layout.fillWidth: true
                active: root.filesContent !== null
                visible: active
                sourceComponent: root.filesContent
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: "Token 统计"
                    color: Theme.textTitle
                    font.pixelSize: 17
                    font.bold: true
                }
                Label {
                    text: root.globalUsage.loading ? "扫描中…"
                          : root.globalUsage.updatedAt ? root.globalUsage.updatedAt + " 更新" : "等待扫描"
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: 38
                radius: 8
                color: Theme.surfaceSunken
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: Theme.accent
                    }
                    Basic.ComboBox {
                        id: usageProfileSelector
                        objectName: "usageProfileSelector"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root.profileOptions
                        currentIndex: Math.max(0, root.profileOptions.indexOf(root.usageModel
                                                                            ? root.usageModel.tokenUsageProfile : ""))
                        enabled: root.profileOptions.length > 1
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.text: enabled ? "筛选 Token 统计 Profile" : "当前仅发现一个 Profile"
                        onActivated: index => root.chooseUsageProfile(root.profileOptions[index])
                        contentItem: Label {
                            text: root.profileLabel(usageProfileSelector.currentText)
                            color: Theme.textBody
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }
                        delegate: Basic.ItemDelegate {
                            id: profileDelegate
                            required property string modelData
                            width: usageProfileSelector.width
                            contentItem: Label {
                                text: root.profileLabel(profileDelegate.modelData)
                                color: Theme.textBody
                                elide: Text.ElideMiddle
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    Label {
                        text: (root.globalUsage.allSessionCount ?? 0) + " 个"
                        color: Theme.textMuted
                        font.pixelSize: 9
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: 34
                radius: 7
                color: Theme.surfaceSunken
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    spacing: 2
                    Repeater {
                        model: root.periodOptions
                        delegate: Basic.Button {
                            id: periodButton
                            required property var modelData
                            objectName: "usagePeriod_" + periodButton.modelData.value
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            leftPadding: 2
                            rightPadding: 2
                            hoverEnabled: true
                            onClicked: root.choosePeriod(periodButton.modelData.value)
                            background: Rectangle {
                                radius: 6
                                color: root.globalUsage.period === periodButton.modelData.value
                                       ? Theme.surfaceRaised : "transparent"
                                border.color: root.globalUsage.period === periodButton.modelData.value
                                              ? Theme.accentBorder : "transparent"
                            }
                            contentItem: Label {
                                text: periodButton.modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: root.globalUsage.period === periodButton.modelData.value
                                       ? Theme.accent : Theme.textSecondary
                                font.pixelSize: 9
                                font.weight: root.globalUsage.period === periodButton.modelData.value
                                             ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                Basic.ToolButton {
                    objectName: "usagePreviousPeriod"
                    text: "‹"
                    enabled: !!root.globalUsage.canMovePrevious
                    implicitWidth: 28
                    implicitHeight: 28
                    font.pixelSize: 20
                    ToolTip.visible: hovered
                    ToolTip.text: "上一周期"
                    onClicked: root.usageModel.shiftTokenUsagePeriod(-1)
                }
                Label {
                    Layout.fillWidth: true
                    text: root.globalUsage.rangeLabel || "—"
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
                Basic.ToolButton {
                    objectName: "usageNextPeriod"
                    text: "›"
                    enabled: !!root.globalUsage.canMoveNext
                    implicitWidth: 28
                    implicitHeight: 28
                    font.pixelSize: 20
                    ToolTip.visible: hovered
                    ToolTip.text: "下一周期"
                    onClicked: root.usageModel.shiftTokenUsagePeriod(1)
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: "全部会话累计"
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textMuted
                font.pixelSize: 9
            }
            Label {
                objectName: "globalTokenTotal"
                Layout.fillWidth: true
                Layout.topMargin: 2
                text: root.formatTokens(root.globalTokens.total)
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textTitle
                font.pixelSize: 25
                font.family: "Cascadia Mono"
                font.weight: Font.DemiBold
                minimumPixelSize: 15
                fontSizeMode: Text.Fit
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                Label {
                    Layout.fillWidth: true
                    text: "$" + root.numberOrZero(root.globalUsage.cost).toFixed(4)
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.success
                    font.pixelSize: 11
                    font.family: "Cascadia Mono"
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: (root.globalUsage.sessionCount ?? 0) + " 个会话"
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
            }

            Item {
                id: trendChart
                objectName: "tokenTrendChart"
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.preferredHeight: 118
                readonly property real maximum: root.maximumTrendValue(root.trendPoints)
                readonly property real barSpacing: root.trendPoints.length > 48 ? 1 : 3

                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        x: 0
                        y: index * 34
                        width: trendChart.width
                        height: 1
                        color: Theme.borderSoft
                    }
                }

                Row {
                    id: trendBars
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 92
                    spacing: trendChart.barSpacing
                    Repeater {
                        model: root.trendPoints
                        Rectangle {
                            required property var modelData
                            required property int index
                            width: Math.max(1, (trendBars.width
                                                - trendChart.barSpacing * Math.max(0, root.trendPoints.length - 1))
                                                / Math.max(1, root.trendPoints.length))
                            height: Math.max(2, 86 * root.numberOrZero(modelData.total) / trendChart.maximum)
                            anchors.bottom: parent.bottom
                            radius: Math.min(2, width / 2)
                            color: index === root.trendPoints.length - 1 ? Theme.accent : Theme.success
                            opacity: root.numberOrZero(modelData.total) > 0 ? 0.9 : 0.18
                            ToolTip.visible: trendPointMouse.containsMouse
                            ToolTip.text: modelData.label + " · " + root.formatTokens(modelData.total) + " tokens"
                            MouseArea {
                                id: trendPointMouse
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    text: root.trendPoints.length ? root.trendPoints[0].label : "—"
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
                Label {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    text: root.trendPoints.length ? root.trendPoints[root.trendPoints.length - 1].label : "—"
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
                Label {
                    anchors.centerIn: parent
                    visible: !root.globalUsage.loading && !root.globalUsage.hasData
                    text: "所选时段暂无用量"
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: 1
                color: Theme.divider
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Label {
                    Layout.fillWidth: true
                    text: "Token 构成"
                    color: Theme.textBody
                    font.pixelSize: 11
                    font.bold: true
                }
                Label {
                    text: "所选时段"
                    color: Theme.textMuted
                    font.pixelSize: 9
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: 8
                radius: 4
                clip: true
                color: Theme.border
                Row {
                    anchors.fill: parent
                    Rectangle {
                        width: parent.width * root.numberOrZero(root.globalTokens.input) / Math.max(1, root.globalTotal)
                        height: parent.height
                        color: "#4f7d78"
                    }
                    Rectangle {
                        width: parent.width * root.numberOrZero(root.globalTokens.output) / Math.max(1, root.globalTotal)
                        height: parent.height
                        color: Theme.accent
                    }
                    Rectangle {
                        width: parent.width * root.numberOrZero(root.globalTokens.cacheRead) / Math.max(1, root.globalTotal)
                        height: parent.height
                        color: Theme.success
                    }
                    Rectangle {
                        width: parent.width * root.numberOrZero(root.globalTokens.cacheWrite) / Math.max(1, root.globalTotal)
                        height: parent.height
                        color: "#5d7192"
                    }
                }
            }

            MetricRow { markerColor: "#4f7d78"; label: "输入"; value: root.globalTokens.input }
            MetricRow { markerColor: Theme.accent; label: "输出"; value: root.globalTokens.output }
            MetricRow { markerColor: Theme.success; label: "缓存读取"; value: root.globalTokens.cacheRead }
            MetricRow { markerColor: "#5d7192"; label: "缓存写入"; value: root.globalTokens.cacheWrite }

            Label {
                Layout.fillWidth: true
                visible: !!root.globalUsage.error
                text: root.globalUsage.error || ""
                wrapMode: Text.Wrap
                color: Theme.dangerText
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: 1
                color: Theme.divider
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Label {
                    Layout.fillWidth: true
                    text: "当前会话"
                    color: Theme.textBody
                    font.pixelSize: 11
                    font.bold: true
                }
                Label {
                    text: root.formatCompactTokens(root.tokenUsage.total) + " tokens"
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: "Cascadia Mono"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                implicitHeight: 7
                radius: 4
                color: Theme.border
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(100, root.contextKnown
                                                              ? root.contextUsage.percent : 0)) / 100
                    height: parent.height
                    radius: 4
                    color: Theme.accent
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Label {
                    Layout.fillWidth: true
                    text: root.contextKnown
                          ? "Context " + root.formatCompactTokens(root.contextUsage.tokens)
                            + " / " + root.formatCompactTokens(root.contextUsage.contextWindow)
                          : "Context —"
                    color: Theme.textSecondary
                    font.pixelSize: 9
                }
                Label {
                    text: root.contextKnown ? Number(root.contextUsage.percent).toFixed(1) + "%" : "—"
                    color: root.contextKnown ? Theme.accent : Theme.textMuted
                    font.pixelSize: 10
                    font.family: "Cascadia Mono"
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 14
                Layout.bottomMargin: 18
                implicitHeight: 48
                radius: 7
                color: Theme.accentSofter
                border.color: Theme.accentBorder
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 0
                    Label {
                        text: "统计范围"
                        color: Theme.accent
                        font.pixelSize: 9
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "统计 Profile · " + root.profileLabel(root.globalUsage.profile
                                                               || (root.usageModel ? root.usageModel.tokenUsageProfile : ""))
                        color: Theme.textBody
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Basic.Dialog {
        id: customRangeDialog
        objectName: "customUsageRangeDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 24, 320)
        modal: true
        focus: true
        title: "自定义统计范围"
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        ColumnLayout {
            width: parent.width
            spacing: 8
            Label { text: "开始日期"; color: Theme.textBody; font.pixelSize: 11 }
            TextField {
                id: customFrom
                objectName: "customUsageFrom"
                Layout.fillWidth: true
                placeholderText: "YYYY-MM-DD"
                selectByMouse: true
            }
            Label { text: "结束日期"; color: Theme.textBody; font.pixelSize: 11 }
            TextField {
                id: customTo
                objectName: "customUsageTo"
                Layout.fillWidth: true
                placeholderText: "YYYY-MM-DD"
                selectByMouse: true
            }
            Label {
                Layout.fillWidth: true
                visible: !!root.globalUsage.error
                text: root.globalUsage.error || ""
                wrapMode: Text.Wrap
                color: Theme.dangerText
                font.pixelSize: 10
            }
        }

        footer: DialogButtonBox {
            Basic.Button { text: "取消"; onClicked: customRangeDialog.close() }
            Basic.Button { text: "应用"; onClicked: root.applyCustomRange() }
        }
    }

    /**
     * 绘制 Outline 中的一项，并通过颜色标记当前位置。
     */
    component OutlineItem: Item {
        id: outlineRoot
        required property string icon
        required property string title
        required property string subtitle
        property bool active: false
        property bool bottomLine: true
        Layout.fillWidth: true
        implicitHeight: 58

        RowLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: outlineRoot.icon
                color: outlineRoot.active ? Theme.accent : Theme.textSecondary
                font.pixelSize: 16
                Layout.preferredWidth: 22
                horizontalAlignment: Text.AlignHCenter
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: outlineRoot.title
                    color: Theme.textBody
                    font.pixelSize: 13
                    font.bold: outlineRoot.active
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: outlineRoot.subtitle
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            visible: outlineRoot.bottomLine
            x: 10
            y: outlineRoot.height - 1
            width: 1
            height: 14
            color: Theme.divider
        }
    }

    /**
     * 绘制相关文件摘要。
     */
    component FileItem: Rectangle {
        id: fileRoot
        required property string icon
        required property string name
        required property string path
        required property color accent
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 62
        radius: 8
        color: "transparent"
        border.color: Theme.borderSoft

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 7
                color: Theme.chipBg
                Label { anchors.centerIn: parent; text: fileRoot.icon; color: fileRoot.accent; font.pixelSize: 16 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { Layout.fillWidth: true; text: fileRoot.name; color: Theme.textBody; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: fileRoot.path; color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideMiddle }
            }
            ToolButton { text: "⋯"; implicitWidth: 24; implicitHeight: 24 }
        }
    }
}
