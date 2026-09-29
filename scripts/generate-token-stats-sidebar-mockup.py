"""生成 Pi Desktop 全局 Token 统计栏的静态 UI 设计稿。"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "ui" / "token-stats-sidebar-global-concept.png"
ASSET = ROOT / "assets" / "pi-desktop.png"
CHINESE_FONT = Path("C:/Windows/Fonts/msyh.ttc")
CHINESE_BOLD_FONT = Path("C:/Windows/Fonts/msyhbd.ttc")
MONO_FONT = Path("C:/Windows/Fonts/CascadiaMono.ttf")

COLORS = {
    "window": "#f7f5f0",
    "surface": "#fbfaf7",
    "surface_alt": "#faf9f6",
    "sunken": "#f8f6f1",
    "sidebar": "#f3f0e9",
    "raised": "#ffffff",
    "footer": "#f4f1eb",
    "border": "#e5e0d7",
    "border_soft": "#ebe7df",
    "border_strong": "#d8d1c7",
    "divider": "#ded9d0",
    "title": "#302d29",
    "primary": "#34312c",
    "body": "#403d37",
    "secondary": "#77736c",
    "muted": "#9b958b",
    "faint": "#aaa49a",
    "accent": "#a96346",
    "accent_soft": "#f0e0d3",
    "accent_softer": "#f6ece3",
    "accent_border": "#dfbba2",
    "success": "#43835c",
    "success_soft": "#e2f1e8",
    "teal": "#4f7d78",
    "blue": "#5d7192",
    "gold": "#aa7b38",
    "user": "#eee9df",
    "code": "#f3f0e9",
}


def font(size: int, bold: bool = False, mono: bool = False) -> ImageFont.FreeTypeFont:
    """按文本用途加载中文或等宽字体。"""
    path = MONO_FONT if mono else CHINESE_BOLD_FONT if bold else CHINESE_FONT
    return ImageFont.truetype(str(path), size=size)


def rounded(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], radius: int,
            fill: str, outline: str | None = None, width: int = 1) -> None:
    """绘制带可选描边的圆角矩形。"""
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], value: str, size: int,
         color: str, bold: bool = False, mono: bool = False,
         anchor: str | None = None) -> None:
    """使用统一字体策略绘制单行文本。"""
    draw.text(xy, value, font=font(size, bold=bold, mono=mono), fill=color, anchor=anchor)


def line(draw: ImageDraw.ImageDraw, xy: tuple[int, int, int, int], color: str,
         width: int = 1) -> None:
    """绘制界面分隔线。"""
    draw.line(xy, fill=color, width=width)


def draw_header(image: Image.Image, draw: ImageDraw.ImageDraw) -> None:
    """绘制应用页头。"""
    draw.rectangle((0, 0, 1440, 52), fill=COLORS["surface"])
    line(draw, (0, 51, 1440, 51), COLORS["border"])

    logo = Image.open(ASSET).convert("RGBA")
    logo.thumbnail((38, 38), Image.Resampling.LANCZOS)
    image.alpha_composite(logo, (18, 7))
    text(draw, (68, 26), "Pi", 15, COLORS["primary"], bold=True, anchor="lm")
    line(draw, (102, 16, 102, 36), COLORS["divider"])
    text(draw, (116, 26), "E:/Tools/pi_Desktop", 12, COLORS["secondary"], anchor="lm")

    text(draw, (1182, 26), "openai / gpt-5.6-sol", 11, COLORS["secondary"], anchor="rm")
    rounded(draw, (1198, 13, 1267, 39), 13, COLORS["success_soft"])
    draw.ellipse((1210, 23, 1216, 29), fill=COLORS["success"])
    text(draw, (1221, 26), "Ready", 10, COLORS["success"], bold=True, anchor="lm")

    # 设置按钮使用齿轮轮廓，避免字体图标在不同环境中变形。
    cx, cy = 1398, 26
    draw.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), outline=COLORS["secondary"], width=2)
    draw.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), outline=COLORS["secondary"], width=1)
    for dx, dy in ((0, -10), (0, 10), (-10, 0), (10, 0)):
        draw.line((cx + dx * 0.6, cy + dy * 0.6, cx + dx, cy + dy), fill=COLORS["secondary"], width=2)


def draw_left_sidebar(draw: ImageDraw.ImageDraw) -> None:
    """绘制左侧会话导航，提供完整三栏场景。"""
    draw.rectangle((0, 52, 264, 872), fill=COLORS["sidebar"])
    line(draw, (263, 52, 263, 872), COLORS["border"])

    rounded(draw, (16, 68, 248, 102), 8, COLORS["accent_softer"], COLORS["accent_border"])
    text(draw, (31, 85), "+", 18, COLORS["accent"], bold=True, anchor="lm")
    text(draw, (54, 85), "新建对话", 12, COLORS["accent"], bold=True, anchor="lm")

    rounded(draw, (16, 114, 248, 148), 7, COLORS["raised"], COLORS["border"])
    draw.ellipse((29, 124, 40, 135), outline=COLORS["muted"], width=1)
    line(draw, (38, 134, 43, 139), COLORS["muted"])
    text(draw, (51, 131), "搜索项目、文件夹或会话", 10, COLORS["muted"], anchor="lm")

    draw.polygon(((17, 174), (25, 174), (21, 179)), fill=COLORS["secondary"])
    text(draw, (34, 177), "PI DESKTOP", 11, COLORS["body"], bold=True, anchor="lm")
    text(draw, (234, 177), "•••", 12, COLORS["muted"], anchor="mm")

    rounded(draw, (10, 194, 254, 240), 6, "#e7e2d8")
    draw.rectangle((10, 194, 13, 240), fill=COLORS["accent"])
    draw.polygon(((28, 205), (36, 205), (32, 210)), fill=COLORS["secondary"])
    text(draw, (45, 208), "pi_Desktop", 12, COLORS["body"], anchor="lm")
    text(draw, (45, 226), "E:/Tools/pi_Desktop", 9, COLORS["muted"], anchor="lm")

    sessions = [
        ("Token 统计侧栏设计", "刚刚", True),
        ("修复 Prompt 快捷键", "28 分钟", False),
        ("DeepSeek 余额动画", "昨天", False),
        ("会话分组交互优化", "9 月 27", False),
    ]
    y = 250
    for title_value, time_value, active in sessions:
        if active:
            rounded(draw, (30, y, 254, y + 54), 6, "#e9e4da")
        line(draw, (30, y + 7, 30, y + 47), "#e4dfd3")
        text(draw, (43, y + 17), title_value, 11, COLORS["primary"], bold=active, anchor="lm")
        text(draw, (43, y + 37), "继续当前项目的实现与验证", 9, COLORS["muted"], anchor="lm")
        text(draw, (242, y + 37), time_value, 8, COLORS["faint"], anchor="rm")
        y += 58

    line(draw, (16, 815, 248, 815), COLORS["divider"])
    text(draw, (18, 835), "当前工作目录", 9, COLORS["muted"], anchor="lm")
    text(draw, (18, 854), "E:/Tools/pi_Desktop", 10, COLORS["secondary"], anchor="lm")


def draw_nav_button(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], label: str,
                    selected: bool = False, plus: bool = False) -> None:
    """绘制工作区顶部导航按钮。"""
    fill = COLORS["accent_soft"] if selected else COLORS["accent_softer"] if plus else "#f7f4ef"
    border = COLORS["accent_border"] if selected else COLORS["border"]
    ink = COLORS["accent"] if selected or plus else COLORS["secondary"]
    rounded(draw, box, 8, fill, border)
    x1, y1, _, y2 = box
    cy = (y1 + y2) // 2
    if plus:
        line(draw, (x1 + 14, cy, x1 + 24, cy), ink, 2)
        line(draw, (x1 + 19, cy - 5, x1 + 19, cy + 5), ink, 2)
        tx = x1 + 34
    else:
        draw.rectangle((x1 + 12, cy - 6, x1 + 26, cy + 6), outline=ink, width=1)
        line(draw, (x1 + 17, cy - 5, x1 + 17, cy + 5), ink)
        tx = x1 + 34
    text(draw, (tx, cy), label, 11, ink, bold=selected, anchor="lm")


def draw_conversation(draw: ImageDraw.ImageDraw) -> None:
    """绘制中间对话区域并弱化无关信息。"""
    x1, x2 = 264, 1134
    draw.rectangle((x1, 52, x2, 872), fill=COLORS["surface_alt"])
    draw.rectangle((x1, 52, x2, 108), fill=COLORS["surface_alt"])
    line(draw, (x1, 107, x2, 107), COLORS["border_soft"])

    draw_nav_button(draw, (282, 63, 354, 97), "会话")
    draw_nav_button(draw, (362, 63, 448, 97), "新对话", plus=True)
    text(draw, (470, 80), "Token 统计侧栏设计", 13, COLORS["primary"], bold=True, anchor="lm")
    draw_nav_button(draw, (1032, 63, 1116, 97), "上下文", selected=True)

    content_x1, content_x2 = 368, 1030
    text(draw, (content_x1, 150), "User", 11, COLORS["secondary"], bold=True)
    rounded(draw, (content_x1, 174, content_x2, 228), 8, COLORS["user"])
    text(draw, (content_x1 + 18, 190), "不只要当前会话累计，还要做整个 Pi 所有会话的累计，", 13, COLORS["body"])
    text(draw, (content_x1 + 18, 212), "并且可以按时间段查看，参考 GitHub 上的 TokenTracker。", 13, COLORS["body"])

    text(draw, (content_x1, 270), "Pi", 11, COLORS["accent"], bold=True)
    text(draw, (content_x1, 299), "右侧栏已调整为 Pi 全局用量视图。", 13, COLORS["primary"])
    text(draw, (content_x1, 326), "当前会话保留在底部，用于和所选时间范围的全局数据对比。", 13, COLORS["primary"])
    text(draw, (content_x1, 353), "窄栏只保留最关键的趋势与构成，避免压缩对话阅读宽度。", 13, COLORS["primary"])

    rounded(draw, (content_x1, 388, content_x2, 483), 7, COLORS["code"], COLORS["border_soft"])
    text(draw, (content_x1 + 16, 407), "统计视图", 10, COLORS["secondary"], bold=True)
    rows = [
        ("scope", "全部 Pi 会话"),
        ("period", "日 / 周 / 月 / 总计 / 自定义"),
        ("metrics", "趋势 / Token 构成 / 费用 / 会话数"),
    ]
    y = 431
    for key, label in rows:
        text(draw, (content_x1 + 16, y), key, 10, COLORS["accent"], mono=True, anchor="lm")
        text(draw, (content_x1 + 160, y), label, 10, COLORS["secondary"], anchor="lm")
        y += 20

    # 输入区保持稳定尺寸，表现实际桌面端布局比例。
    rounded(draw, (320, 716, 1078, 842), 10, COLORS["raised"], COLORS["border_strong"])
    text(draw, (342, 743), "输入消息…", 12, COLORS["faint"], anchor="lm")
    draw.ellipse((340, 796, 368, 824), outline=COLORS["border_strong"], width=1)
    text(draw, (354, 810), "+", 16, COLORS["secondary"], anchor="mm")
    text(draw, (900, 810), "Context 30.0%", 10, COLORS["secondary"], anchor="rm")
    rounded(draw, (986, 793, 1058, 827), 8, COLORS["accent"])
    text(draw, (1022, 810), "发送", 11, "#ffffff", bold=True, anchor="mm")


def draw_progress(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], percent: float) -> None:
    """绘制上下文占用进度条。"""
    rounded(draw, box, 4, COLORS["border"], None)
    x1, y1, x2, y2 = box
    fill_width = max(8, int((x2 - x1) * percent))
    rounded(draw, (x1, y1, x1 + fill_width, y2), 4, COLORS["accent"], None)


def draw_metric_row(draw: ImageDraw.ImageDraw, y: int, color: str, label: str,
                    value: str) -> None:
    """绘制 Token 分类指标行。"""
    draw.ellipse((1152, y - 4, 1160, y + 4), fill=color)
    text(draw, (1168, y), label, 11, COLORS["secondary"], anchor="lm")
    text(draw, (1420, y), value, 11, COLORS["body"], bold=True, mono=True, anchor="rm")


def draw_right_panel(draw: ImageDraw.ImageDraw) -> None:
    """绘制支持全局会话聚合与时间筛选的右侧 Token 面板。"""
    x1, x2 = 1134, 1440
    draw.rectangle((x1, 52, x2, 872), fill=COLORS["surface"])
    line(draw, (x1, 52, x1, 872), COLORS["border"])

    text(draw, (1150, 80), "Token 统计", 17, COLORS["title"], bold=True, anchor="lm")
    text(draw, (1424, 80), "刚刚更新", 9, COLORS["muted"], anchor="rm")

    # 全局范围选择器可继续扩展为按项目或工作目录过滤。
    rounded(draw, (1150, 101, 1424, 137), 7, COLORS["sunken"], COLORS["border"])
    draw.ellipse((1163, 115, 1171, 123), fill=COLORS["accent"])
    text(draw, (1180, 119), "全部 Pi 会话", 11, COLORS["body"], bold=True, anchor="lm")
    text(draw, (1387, 119), "34 个", 9, COLORS["muted"], anchor="rm")
    draw.polygon(((1403, 116), (1411, 116), (1407, 121)), fill=COLORS["secondary"])

    # 参考 TokenTracker 的 Day / Week / Month / Total / Custom 五档时间范围。
    period_labels = ["日", "周", "月", "总计", "自定义"]
    period_left, period_top, period_width = 1150, 149, 274
    segment_width = period_width / len(period_labels)
    rounded(draw, (period_left, period_top, period_left + period_width, period_top + 34),
            7, COLORS["sunken"], COLORS["border"])
    for index, label in enumerate(period_labels):
        sx1 = round(period_left + index * segment_width)
        sx2 = round(period_left + (index + 1) * segment_width)
        selected = label == "月"
        if selected:
            rounded(draw, (sx1 + 2, period_top + 2, sx2 - 2, period_top + 32),
                    6, COLORS["raised"], COLORS["accent_border"])
        text(draw, ((sx1 + sx2) // 2, period_top + 17), label, 9,
             COLORS["accent"] if selected else COLORS["secondary"],
             bold=selected, anchor="mm")

    # 月份可前后翻页，自定义范围则在同一区域替换成起止日期。
    line(draw, (1154, 203, 1162, 195), COLORS["secondary"], 1)
    line(draw, (1154, 203, 1162, 211), COLORS["secondary"], 1)
    text(draw, (1287, 203), "2026年9月", 10, COLORS["secondary"], anchor="mm")
    line(draw, (1420, 203, 1412, 195), COLORS["secondary"], 1)
    line(draw, (1420, 203, 1412, 211), COLORS["secondary"], 1)

    text(draw, (1287, 231), "全部会话累计", 9, COLORS["muted"], anchor="mm")
    text(draw, (1287, 263), "12,842,560", 24, COLORS["title"], bold=True, mono=True, anchor="mm")
    text(draw, (1216, 291), "$42.73", 12, COLORS["success"], bold=True, mono=True, anchor="mm")
    text(draw, (1357, 291), "34 个会话", 10, COLORS["secondary"], anchor="mm")

    # 窄栏趋势图使用日聚合柱，足够判断峰值与低谷，不承载详细表格。
    chart_left, chart_top, chart_right, chart_bottom = 1150, 319, 1424, 414
    for gy in (chart_top, chart_top + 35, chart_top + 70):
        line(draw, (chart_left, gy, chart_right, gy), COLORS["border_soft"])
    trend = [26, 38, 18, 44, 31, 60, 52, 78, 42, 35, 68, 84, 48, 72, 57, 91, 64, 46, 76, 88]
    gap = 4
    bar_width = 10
    for index, value in enumerate(trend):
        bx = chart_left + index * (bar_width + gap)
        height = round((chart_bottom - chart_top - 8) * value / 100)
        color = COLORS["accent"] if index == len(trend) - 1 else COLORS["teal"]
        rounded(draw, (bx, chart_bottom - height, bx + bar_width, chart_bottom), 2, color)
    text(draw, (chart_left, 431), "9月1日", 9, COLORS["muted"], anchor="lm")
    text(draw, (chart_right, 431), "9月29日", 9, COLORS["muted"], anchor="rm")

    line(draw, (1150, 451, 1424, 451), COLORS["divider"])
    text(draw, (1150, 477), "Token 构成", 11, COLORS["body"], bold=True, anchor="lm")
    text(draw, (1424, 477), "所选时段", 9, COLORS["muted"], anchor="rm")

    bar_x1, bar_x2, bar_y1, bar_y2 = 1150, 1424, 496, 504
    values = [4150000, 1092560, 7200000, 400000]
    bar_colors = [COLORS["teal"], COLORS["accent"], COLORS["success"], COLORS["blue"]]
    total = sum(values)
    cursor = bar_x1
    for index, (value, color) in enumerate(zip(values, bar_colors)):
        next_x = bar_x2 if index == len(values) - 1 else cursor + round((bar_x2 - bar_x1) * value / total)
        draw.rectangle((cursor, bar_y1, next_x, bar_y2), fill=color)
        cursor = next_x

    draw_metric_row(draw, 536, COLORS["teal"], "输入", "4.15M")
    draw_metric_row(draw, 568, COLORS["accent"], "输出", "1.09M")
    draw_metric_row(draw, 600, COLORS["success"], "缓存读取", "7.20M")
    draw_metric_row(draw, 632, COLORS["blue"], "缓存写入", "400K")

    line(draw, (1150, 658, 1424, 658), COLORS["divider"])
    text(draw, (1150, 684), "当前会话", 11, COLORS["body"], bold=True, anchor="lm")
    text(draw, (1424, 684), "105,000 tokens", 10, COLORS["secondary"], mono=True, anchor="rm")
    draw_progress(draw, (1150, 706, 1424, 713), 0.30)
    text(draw, (1150, 734), "Context 60,000 / 200,000", 9, COLORS["secondary"], anchor="lm")
    text(draw, (1424, 734), "30.0%", 10, COLORS["accent"], bold=True, mono=True, anchor="rm")

    rounded(draw, (1150, 764, 1424, 810), 7, COLORS["accent_softer"], COLORS["accent_border"])
    text(draw, (1164, 780), "统计范围", 9, COLORS["accent"], bold=True, anchor="lm")
    text(draw, (1164, 799), "当前 Profile · 全部 Pi Session", 9, COLORS["body"], anchor="lm")


def draw_footer(draw: ImageDraw.ImageDraw) -> None:
    """绘制底部连接状态栏。"""
    draw.rectangle((0, 872, 1440, 900), fill=COLORS["footer"])
    line(draw, (0, 872, 1440, 872), COLORS["border"])
    draw.ellipse((18, 883, 25, 890), fill=COLORS["success"])
    text(draw, (34, 887), "Ready", 10, COLORS["secondary"], anchor="lm")
    text(draw, (1198, 887), "Profile: C:/Users/29016/.pi/profiles/Hocchin", 9, COLORS["faint"], anchor="rm")
    text(draw, (1422, 887), "DeepSeek ¥110.00", 10, COLORS["success"], bold=True, anchor="rm")


def main() -> None:
    """组合各区域并输出 PNG 设计稿。"""
    image = Image.new("RGBA", (1440, 900), COLORS["window"])
    draw = ImageDraw.Draw(image)
    draw_header(image, draw)
    draw_left_sidebar(draw)
    draw_conversation(draw)
    draw_right_panel(draw)
    draw_footer(draw)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGB").save(OUTPUT, quality=95, optimize=True)
    print(OUTPUT)


if __name__ == "__main__":
    main()
