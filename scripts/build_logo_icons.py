"""Recolors the engine logo into each Weezer album colour and writes a 512 px PNG plus a multi-size .ico per colour.

Run: python scripts/build_logo_icons.py
The W coverage is derived from scripts/../resources/logo/WillEngine_blue.png, so that file is both an output and the mask source.
"""
from collections import Counter
from pathlib import Path

from PIL import Image

LOGO_DIR = Path(__file__).resolve().parent.parent / "resources" / "logo"
MASK_SOURCE = LOGO_DIR / "WillEngine_blue.png"
BLACK_INK = (0, 0, 0)
WHITE_INK = (243, 243, 243)
ICO_SIZES = [256, 128, 64, 48, 32, 24, 16]
# Tiny sizes get a larger W so the inner V survives downsampling; larger sizes keep the true proportions
SMALL_SIZE_GLYPH_SCALE = {32: 1.1, 24: 1.2, 16: 1.2}

# (name, background, ink); hexes sampled from the album covers
VARIANTS = [
    ("blue", (0x18, 0x9B, 0xCC), BLACK_INK),
    ("green", (0xBE, 0xCC, 0x41), BLACK_INK),
    ("red", (0xEA, 0x21, 0x3A), BLACK_INK),
    ("white", (0xF3, 0xF3, 0xF3), BLACK_INK),
    ("teal", (0x2B, 0xBC, 0xBB), BLACK_INK),
    ("black", (0x0D, 0x0D, 0x0D), WHITE_INK),
]


def luminance(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def load_coverage():
    image = Image.open(MASK_SOURCE).convert("RGB")
    pixels = image.load()
    background = Counter(image.getdata()).most_common(1)[0][0]
    ink = min(Counter(image.getdata()).most_common(8), key=lambda entry: luminance(entry[0]))[0]
    background_lum = luminance(background)
    ink_lum = luminance(ink)
    width, height = image.size
    coverage = Image.new("L", image.size)
    out = coverage.load()
    for y in range(height):
        for x in range(width):
            t = (background_lum - luminance(pixels[x, y])) / (background_lum - ink_lum)
            out[x, y] = round(min(max(t, 0.0), 1.0) * 255)
    return centered_glyph(coverage, 1.0)


def centered_glyph(coverage, scale):
    glyph = coverage.crop(coverage.getbbox())
    if scale != 1.0:
        glyph = glyph.resize((round(glyph.width * scale), round(glyph.height * scale)), Image.LANCZOS)
    centered = Image.new("L", coverage.size)
    centered.paste(glyph, ((coverage.width - glyph.width) // 2, (coverage.height - glyph.height) // 2))
    return centered


def render(coverage, background, ink):
    return Image.composite(Image.new("RGBA", coverage.size, ink + (255,)), Image.new("RGBA", coverage.size, background + (255,)), coverage)


def render_icon(coverage, background, ink, size):
    scale = SMALL_SIZE_GLYPH_SCALE.get(size)
    if scale:
        coverage = centered_glyph(coverage, scale)
    return render(coverage, background, ink).resize((size, size), Image.LANCZOS)


def main():
    coverage = load_coverage()
    for name, background, ink in VARIANTS:
        render(coverage, background, ink).save(LOGO_DIR / f"WillEngine_{name}.png")
        icons = [render_icon(coverage, background, ink, size) for size in ICO_SIZES]
        icons[0].save(LOGO_DIR / f"WillEngine_{name}.ico", format="ICO", sizes=[(size, size) for size in ICO_SIZES], append_images=icons[1:])
        print(f"WillEngine_{name}: {'#%02X%02X%02X' % background}")


if __name__ == "__main__":
    main()
