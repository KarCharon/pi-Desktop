"""用 Pillow 复刻应用左上角的米色圆角 Pi 标识，生成 PNG 和多尺寸 ICO。"""

import argparse
import logging
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


LOG = logging.getLogger("AppIcon")


def render_icon(font_path: Path) -> Image.Image:
    """按原 QML 的 27:13 尺寸比例绘制标识，超采样保持小尺寸边缘平滑。"""
    size = 2048
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, size - 1, size - 1), radius=round(size * 7 / 27), fill="#e7d4b8")
    font = ImageFont.truetype(str(font_path), round(size * 13 / 27))
    # 使用字形包围盒居中，避免字体基线和留白导致视觉偏移。
    left, top, right, bottom = draw.textbbox((0, 0), "Pi", font=font)
    position = ((size - (right - left)) / 2 - left, (size - (bottom - top)) / 2 - top)
    draw.text(position, "Pi", font=font, fill="#4a3629")
    return image.resize((512, 512), Image.Resampling.LANCZOS)


def main() -> None:
    """校验字体并生成可直接随源码构建的资源，不要求构建机安装 Python。"""
    logging.basicConfig(level=logging.INFO, format="[AppIcon] %(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path,
                        default=Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/timesbd.ttf")
    args = parser.parse_args()
    if not args.font.is_file():
        parser.error(f"字体文件不存在：{args.font}；请用 --font 指定粗体衬线字体")
    output = Path(__file__).resolve().parent.parent / "assets"
    try:
        image = render_icon(args.font)
        output.mkdir(parents=True, exist_ok=True)
        image.save(output / "pi-desktop.png")
        sizes = [(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)]
        image.save(output / "pi-desktop.ico", format="ICO", sizes=sizes)
        # 生成后立即检查帧尺寸与透明角，避免损坏图标进入发布包。
        with Image.open(output / "pi-desktop.ico") as icon:
            if icon.ico.sizes() != set(sizes):
                raise ValueError("ICO 帧尺寸不完整")
        if image.getpixel((0, 0))[3] != 0:
            raise ValueError("PNG 圆角透明度无效")
        LOG.info("生成成功；PNG=512x512；ICO=%d 个尺寸；font=%s；output=%s", len(sizes), args.font, output)
    except (OSError, ValueError):
        LOG.exception("图标生成或验证失败")
        raise


if __name__ == "__main__":
    main()
