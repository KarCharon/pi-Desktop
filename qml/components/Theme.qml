pragma Singleton

import QtQuick

/**
 * 全局主题单例，集中维护浅色与深色两套语义色令牌，以及对话区缩放系数。
 *
 * 使用方式：Main.qml 根据用户设置写入 `Theme.dark`，其余组件只读取令牌，
 * 不再直接写死十六进制颜色，从而保证深浅主题切换时界面整体一致。
 * 令牌命名遵循「用途」而非「色值」，方便后续替换具体色号。
 * 对话区字号与相关间距统一通过 `Theme.scaled()` 换算，避免各处重复写缩放公式。
 */
QtObject {
    /** 是否启用深色配色；由 Main.qml 与用户设置双向同步。 */
    property bool dark: false

    // ── 对话区缩放 ──────────────────────────────────────────────
    /** 对话区缩放系数，1.0 为基准；由 ChatView 的 Ctrl+滚轮调整。 */
    property real chatZoom: 1.0
    /** 对话区缩放下限，避免文字小到无法辨认。 */
    readonly property real chatZoomMinimum: 0.7
    /** 对话区缩放上限，避免单条消息占满整屏。 */
    readonly property real chatZoomMaximum: 2.5
    /** 每次 Ctrl+滚轮触发的缩放倍率，使缩放呈等比的档位感。 */
    readonly property real chatZoomStep: 1.1

    /**
     * 把基准像素值按对话缩放系数换算为实际像素。
     *
     * 用于字号、间距、内边距与圆角等所有随对话缩放的数值；返回值取整以匹配
     * pixelSize、width 等整数属性，缩放为 1.0 时结果与输入相同。
     */
    function scaled(base) {
        return Math.round(base * chatZoom)
    }

    // ── 基础背景 ────────────────────────────────────────────────
    /** 应用窗口底色，承载页头、页脚之外的整体背景。 */
    readonly property color windowBg: dark ? "#1b1917" : "#f7f5f0"
    /** 主内容区与弹窗背景，比窗口底色略亮或略暗形成层次。 */
    readonly property color surface: dark ? "#242220" : "#fbfaf7"
    /** 次级表面：对话主栏、对话框、菜单等。 */
    readonly property color surfaceAlt: dark ? "#201e1c" : "#faf9f6"
    /** 下沉表面：上下文统计卡片等内嵌区块。 */
    readonly property color surfaceSunken: dark ? "#262421" : "#f8f6f1"
    /** 弱化表面：侧栏、代码块、内嵌输入等。 */
    readonly property color surfaceMuted: dark ? "#1f1d1b" : "#f3f0e9"
    /** 抬升表面：输入框、补全弹层等需要更醒目的白色区域。 */
    readonly property color surfaceRaised: dark ? "#2c2926" : "#ffffff"
    /** 左侧会话栏背景。 */
    readonly property color sidebarBg: dark ? "#1f1d1b" : "#f3f0e9"
    /** 底部状态栏背景。 */
    readonly property color footerBg: dark ? "#1a1816" : "#f4f1eb"
    /** 用户消息气泡背景。 */
    readonly property color userBubble: dark ? "#322b24" : "#eee9df"
    /** 代码/工具输出块背景。 */
    readonly property color codeBg: dark ? "#1c1a18" : "#f3f0e9"
    /** 附件芯片、图标底色等弱强调背景。 */
    readonly property color chipBg: dark ? "#2a2723" : "#f1eee8"

    // ── 边框与分隔线 ────────────────────────────────────────────
    /** 常规边框。 */
    readonly property color border: dark ? "#37332e" : "#e5e0d7"
    /** 更明显的边框，用于输入框、弹窗描边。 */
    readonly property color borderStrong: dark ? "#43403a" : "#d8d1c7"
    /** 柔和边框，用于卡片分隔。 */
    readonly property color borderSoft: dark ? "#302d29" : "#ebe7df"
    /** 极浅边框，用于导航按钮默认描边。 */
    readonly property color borderFaint: dark ? "#2c2925" : "#e8e0d5"
    /** 页头/页脚处的分隔线。 */
    readonly property color divider: dark ? "#37332e" : "#ded9d0"

    // ── 文本 ────────────────────────────────────────────────────
    /** 标题文字。 */
    readonly property color textTitle: dark ? "#f2eee6" : "#302d29"
    /** 主要文字，用于正文与消息内容。 */
    readonly property color textPrimary: dark ? "#e6e1d8" : "#34312c"
    /** 常规正文文字。 */
    readonly property color textBody: dark ? "#d3cdc3" : "#403d37"
    /** 次级说明文字。 */
    readonly property color textSecondary: dark ? "#a49d93" : "#77736c"
    /** 弱化文字，用于时间戳、提示。 */
    readonly property color textMuted: dark ? "#877f75" : "#9b958b"
    /** 最弱文字，用于占位与装饰。 */
    readonly property color textFaint: dark ? "#6d675f" : "#aaa49a"
    /** 强调色按钮上的文字（浅深主题均为白色）。 */
    readonly property color textOnAccent: "#ffffff"

    // ── 强调色（暖橙主色） ──────────────────────────────────────
    /** 主强调色，用于图标、链接、关键文字。 */
    readonly property color accent: dark ? "#cf8a5e" : "#a96346"
    /** 强调色悬停态。 */
    readonly property color accentHover: dark ? "#dd9a6e" : "#c47a59"
    /** 强调色按下态。 */
    readonly property color accentPressed: dark ? "#b97850" : "#bd7050"
    /** 强调色弱背景，用于选中项。 */
    readonly property color accentSoft: dark ? "#3d2c20" : "#f0e0d3"
    /** 强调色更弱背景，用于当前项/悬停。 */
    readonly property color accentSofter: dark ? "#33261d" : "#f6ece3"
    /** 强调色描边。 */
    readonly property color accentBorder: dark ? "#5f4530" : "#dfbba2"
    /** 强调色文字（警告型链接）。 */
    readonly property color accentText: dark ? "#e29a72" : "#a95137"

    // ── 危险 / 失败 ─────────────────────────────────────────────
    /** 危险主色。 */
    readonly property color danger: dark ? "#e08a86" : "#b65e5a"
    /** 危险文字色。 */
    readonly property color dangerText: dark ? "#e59a92" : "#a95137"
    /** 危险弱背景。 */
    readonly property color dangerSoft: dark ? "#3a2523" : "#f6e2df"
    /** 危险描边。 */
    readonly property color dangerBorder: dark ? "#6d4340" : "#d9968d"
    /** 更深的危险文字，用于工具失败信息。 */
    readonly property color dangerStrong: dark ? "#e08a86" : "#a34e49"

    // ── 成功 / 警告 ─────────────────────────────────────────────
    /** 成功主色。 */
    readonly property color success: dark ? "#6fbf8b" : "#43835c"
    /** 成功文字色。 */
    readonly property color successText: dark ? "#7fd39a" : "#559467"
    /** 成功弱背景。 */
    readonly property color successSoft: dark ? "#22322a" : "#e2f1e8"
    /** 警告主色。 */
    readonly property color warning: dark ? "#d9a95f" : "#b48445"
    /** 警告描边。 */
    readonly property color warningBorder: dark ? "#6e5a34" : "#d9b47d"

    // ── 助手头像 ────────────────────────────────────────────────
    /** 助手头像底色。 */
    readonly property color avatarBg: dark ? "#3a3126" : "#e7d8bd"
    /** 助手头像文字色。 */
    readonly property color avatarText: dark ? "#e7cfa6" : "#543d2d"

    // ── 滚动条与分栏手柄 ────────────────────────────────────────
    /** 滚动条默认滑块。 */
    readonly property color scrollThumb: dark ? "#453f38" : "#c9c3b9"
    /** 滚动条悬停滑块。 */
    readonly property color scrollThumbHover: dark ? "#57504a" : "#aaa298"
    /** 滚动条按下滑块。 */
    readonly property color scrollThumbActive: dark ? "#b87a54" : "#a77c64"
    /** 分栏手柄默认底色。 */
    readonly property color splitHandle: dark ? "#201e1c" : "#faf9f6"
    /** 分栏手柄悬停底色。 */
    readonly property color splitHandleHover: dark ? "#2a2724" : "#f0ebe3"
    /** 分栏手柄按下底色。 */
    readonly property color splitHandlePressed: dark ? "#3a2b22" : "#e7ddd1"

    // ── 工作区导航按钮 ──────────────────────────────────────────
    /** 导航按钮默认背景。 */
    readonly property color navBg: dark ? "#2a2724" : "#f7f4ef"
    /** 导航按钮悬停背景。 */
    readonly property color navBgHover: dark ? "#322e2a" : "#eee7dd"
    /** 导航按钮选中背景。 */
    readonly property color navBgSelected: dark ? "#3d2c20" : "#f0e0d3"
    /** 导航按钮强调背景。 */
    readonly property color navBgAccent: dark ? "#33261d" : "#f6ece3"
    /** 导航按钮禁用背景。 */
    readonly property color navBgDisabled: dark ? "#242220" : "#f5f2ed"
    /** 导航按钮按下背景。 */
    readonly property color navBgDown: dark ? "#46311f" : "#e8d7c8"
    /** 导航按钮默认描边。 */
    readonly property color navBorder: dark ? "#37332e" : "#e8e0d5"
    /** 导航按钮悬停描边。 */
    readonly property color navBorderHover: dark ? "#474139" : "#d8cabb"
    /** 导航按钮选中描边。 */
    readonly property color navBorderSelected: dark ? "#5f4530" : "#dfbba2"
    /** 导航按钮默认文字。 */
    readonly property color navInk: dark ? "#a79f94" : "#736b60"
    /** 导航按钮激活文字。 */
    readonly property color navInkActive: dark ? "#cf8a5e" : "#a96346"
    /** 导航按钮禁用文字。 */
    readonly property color navInkDisabled: dark ? "#5b5650" : "#b9b1a6"

    // ── 侧栏小按钮与会话条目 ────────────────────────────────────
    /** 侧栏图标按钮文字。 */
    readonly property color sideBtnInk: dark ? "#b9ac97" : "#786451"
    /** 侧栏图标按钮禁用文字。 */
    readonly property color sideBtnInkDisabled: dark ? "#5b5650" : "#b8afa3"
    /** 侧栏图标按钮悬停背景。 */
    readonly property color sideBtnHover: dark ? "#2f2b26" : "#e9e3d9"
    /** 侧栏图标按钮按下背景。 */
    readonly property color sideBtnDown: dark ? "#3a2b22" : "#e1d8cb"
    /** 会话条目选中背景。 */
    readonly property color rowSelected: dark ? "#302c27" : "#e9e4da"
    /** 会话条目悬停背景。 */
    readonly property color rowHover: dark ? "#282420" : "#ebe7df"
    /** 会话目录标题文字。 */
    readonly property color rowFolderInk: dark ? "#b9ac97" : "#786451"

    // ── 系统消息（浅深主题都保持深色点缀，突出状态） ────────────
    /** 系统消息背景。 */
    readonly property color systemBg: "#161c26"
    /** 系统消息描边。 */
    readonly property color systemBorder: "#283143"
    /** 系统消息文字。 */
    readonly property color systemText: "#8793a7"
    /** 失败系统消息背景。 */
    readonly property color systemFailBg: "#301c22"
    /** 失败系统消息描边。 */
    readonly property color systemFailBorder: "#74404a"
    /** 失败系统消息文字。 */
    readonly property color systemFailText: "#ff9aa6"
}
