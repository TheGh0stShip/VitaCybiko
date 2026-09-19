#!/usr/bin/env python3
"""Generate VitaCybiko LiveArea artwork at Vita-required dimensions."""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ICON_PATH = ROOT / "sce_sys" / "icon0.png"
LIVEAREA_DIR = ROOT / "sce_sys" / "livearea" / "contents"
FONT_REGULAR = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
FONT_BOLD = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
FONT_MONO = Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf")


def font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    if path.exists():
        return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def text_size(draw: ImageDraw.ImageDraw, text: str, face: ImageFont.ImageFont) -> tuple[int, int]:
    left, top, right, bottom = draw.textbbox((0, 0), text, font=face)
    return right - left, bottom - top


def paste_icon(canvas: Image.Image, xy: tuple[int, int], size: int, glow: int = 22) -> None:
    icon = Image.open(ICON_PATH).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
    mask = icon.getchannel("A")
    glow_layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    glow_icon = Image.new("RGBA", (size, size), (58, 214, 255, 150))
    glow_layer.paste(glow_icon, xy, mask)
    glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(glow))
    canvas.alpha_composite(glow_layer)
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shadow_icon = Image.new("RGBA", (size, size), (0, 0, 0, 180))
    shadow.paste(shadow_icon, (xy[0] + 8, xy[1] + 12), mask)
    shadow = shadow.filter(ImageFilter.GaussianBlur(10))
    canvas.alpha_composite(shadow)
    canvas.alpha_composite(icon, xy)


def gradient(size: tuple[int, int]) -> Image.Image:
    width, height = size
    image = Image.new("RGBA", size)
    px = image.load()
    for y in range(height):
        for x in range(width):
            t = y / max(1, height - 1)
            r = int(5 + 12 * t + 8 * x / width)
            g = int(13 + 20 * t)
            b = int(23 + 31 * t + 18 * x / width)
            px[x, y] = (r, g, b, 255)
    return image


def draw_scanlines(draw: ImageDraw.ImageDraw, width: int, height: int) -> None:
    for y in range(0, height, 6):
        draw.line((0, y, width, y), fill=(255, 255, 255, 10), width=1)
    for x in range(-height, width, 34):
        draw.line((x, height, x + height, 0), fill=(80, 210, 245, 10), width=1)


def pill(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, face: ImageFont.ImageFont,
         fill: tuple[int, int, int, int], outline: tuple[int, int, int, int]) -> None:
    x, y = xy
    tw, th = text_size(draw, text, face)
    box = (x, y, x + tw + 28, y + th + 16)
    draw.rounded_rectangle(box, radius=14, fill=fill, outline=outline, width=2)
    draw.text((x + 14, y + 7), text, font=face, fill=(232, 252, 255, 255))


def generate_background(version: str) -> Image.Image:
    bg = gradient((840, 500))
    draw = ImageDraw.Draw(bg, "RGBA")
    draw_scanlines(draw, 840, 500)

    draw.rounded_rectangle((34, 34, 806, 466), radius=28,
                           fill=(3, 7, 12, 90), outline=(87, 222, 245, 180), width=3)
    draw.rounded_rectangle((55, 314, 510, 432), radius=18,
                           fill=(15, 34, 43, 205), outline=(96, 215, 238, 100), width=2)
    draw.rounded_rectangle((545, 318, 777, 432), radius=18,
                           fill=(15, 34, 43, 180), outline=(226, 146, 43, 140), width=2)

    title = font(FONT_BOLD, 56)
    subtitle = font(FONT_REGULAR, 22)
    small = font(FONT_REGULAR, 18)
    mono = font(FONT_MONO, 18)

    draw.text((58, 62), "VitaCybiko", font=title, fill=(238, 250, 255, 255))
    draw.text((61, 124), "Cybiko Classic + Xtreme emulator", font=subtitle, fill=(155, 218, 231, 255))
    pill(draw, (60, 164), f"VERSION {version}", mono, (24, 74, 88, 220), (96, 226, 248, 210))
    pill(draw, (245, 164), "VCYB00001", mono, (58, 38, 16, 210), (230, 151, 42, 210))

    draw.text((78, 335), "Boot profiles", font=subtitle, fill=(230, 245, 248, 255))
    draw.text((78, 370), "Classic V1  •  Classic V2  •  Xtreme", font=small, fill=(179, 207, 213, 255))
    draw.text((78, 396), "Firmware stays in ux0:data/VitaCybiko", font=small, fill=(137, 175, 184, 255))

    draw.text((573, 341), "Preservation build", font=small, fill=(236, 222, 185, 255))
    draw.text((573, 370), "No ROMs bundled", font=small, fill=(177, 198, 204, 255))
    draw.text((573, 396), "Install your own files", font=small, fill=(137, 175, 184, 255))

    paste_icon(bg, (575, 62), 205, glow=24)
    return bg.convert("RGB")


def generate_startup(version: str) -> Image.Image:
    img = gradient((280, 158))
    draw = ImageDraw.Draw(img, "RGBA")
    draw_scanlines(draw, 280, 158)
    draw.rounded_rectangle((8, 8, 272, 150), radius=16,
                           fill=(3, 8, 14, 105), outline=(87, 222, 245, 185), width=2)
    paste_icon(img, (16, 25), 96, glow=12)

    title = font(FONT_BOLD, 24)
    small = font(FONT_REGULAR, 13)
    mono = font(FONT_MONO, 14)
    draw.text((118, 36), "VitaCybiko", font=title, fill=(238, 250, 255, 255))
    draw.text((120, 69), "Classic + Xtreme", font=small, fill=(159, 220, 232, 255))
    pill(draw, (120, 96), f"v{version}", mono, (24, 74, 88, 230), (96, 226, 248, 220))
    return img.convert("RGB")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    LIVEAREA_DIR.mkdir(parents=True, exist_ok=True)
    generate_background(args.version).save(LIVEAREA_DIR / "bg.png", optimize=True)
    generate_startup(args.version).save(LIVEAREA_DIR / "startup.png", optimize=True)


if __name__ == "__main__":
    main()
