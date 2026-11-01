"""The project's cover picture, made the Atari ST way: a 320 x 200 low resolution screen in the ST's colours (3
bits a channel), scaled up 4 times with square pixels -> res/cover.png (1280 x 800).
Top and bottom: a tile from each game (crops of screenshots - not in git: pass the folder with them); in the
middle the program's llama (Tools/make_icon.py's own art) on a starfield and the title in the program's 8 x 8
font (SDL's debug font: the public domain IBM VGA one) with a Llamasoft-style raster gradient.
usage: make_cover.py <folder with src_<game>.bmp screenshots> [out.png]      (python -P: see dis.py)
The screenshots: --shot from each game (src_llamatron2, src_gridrunner, src_trip, src_rmc, src_andes,
src_d2title, src_hctitle, src_pstitle); CROPS below says which 80 x 56 part of each is used."""
import importlib.util
import os
import random
import re
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
W, H, SCALE = 320, 200, 4

# (screenshot, left, top) of each 80 x 56 tile; top row then bottom row.
CROPS = [
    ('src_llamatron2', 20, 32), ('src_gridrunner', 120, 72), ('src_trip', 120, 60), ('src_rmc', 240, 144),
    ('src_andes', 132, 114), ('src_d2title', 120, 118), ('src_hctitle', 175, 10), ('src_pstitle', 78, 58),
]


def st(c):
    """An ST colour from 3-bit levels (0..7 a channel), as 8-bit RGB the way the shifter's DAC spreads them."""
    return tuple((v << 5) | (v << 2) | (v >> 1) for v in c)


def to_st(rgb):
    return st(tuple(min(7, round(v / 255 * 7)) for v in rgb))


def font():
    """SDL's 8 x 8 debug font from its header: glyph c - 33 for 33..126, rows top to bottom, bit 0 = left."""
    text = open(os.path.join(ROOT, 'third_party/SDL/src/render/SDL_render_debug_font.h'), encoding='latin-1').read()
    body = text[text.index('SDL_RenderDebugTextFontData[] = {'):]
    data = [int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2}),', body)]
    return lambda ch: data[(ord(ch) - 33) * 8:(ord(ch) - 32) * 8] if 32 < ord(ch) < 127 else [0] * 8


def text(img, s, x, y, scale, colour_of_row, shadow=None):
    glyph = font()
    for i, ch in enumerate(s):
        rows = glyph(ch)
        for gy in range(8):
            for gx in range(8):
                if not rows[gy] >> gx & 1:
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        px, py = x + (i * 8 + gx) * scale + sx, y + gy * scale + sy
                        if shadow and 0 <= px + 1 < W and 0 <= py + 1 < H:
                            img.putpixel((px + 1, py + 1), shadow)
        for gy in range(8):
            for gx in range(8):
                if rows[gy] >> gx & 1:
                    for sy in range(scale):
                        for sx in range(scale):
                            py = y + gy * scale + sy
                            img.putpixel((x + (i * 8 + gx) * scale + sx, py), colour_of_row(py - y))


def llama(img, cx, cy):
    """make_icon.py's llama, doubled, on its purple badge."""
    spec = importlib.util.spec_from_file_location('make_icon', os.path.join(ROOT, 'Tools', 'make_icon.py'))
    icon = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(icon)
    art, n = icon.ART, len(icon.ART)
    fur, eye, badge, rim, rim2 = st((7, 6, 1)), st((1, 0, 2)), st((3, 1, 5)), st((2, 0, 3)), st((6, 3, 7))
    r = n + 2  # badge radius in cover pixels (the art is doubled)
    for y in range(-r - 1, r + 2):
        for x in range(-r - 1, r + 2):
            d = (x * x + y * y) ** 0.5
            if d <= r + 1:
                img.putpixel((cx + x, cy + y), rim2 if d > r else rim if d > r - 2 else badge)
    ox, oy = cx - n, cy - n
    for y, row in enumerate(art):
        for x, ch in enumerate(row):
            if ch == '#':
                for sy in range(2):
                    for sx in range(2):
                        img.putpixel((ox + x * 2 + sx, oy + y * 2 + sy), fur)
    for sy in range(2):
        for sx in range(2):
            img.putpixel((ox + 9 * 2 + sx, oy + 6 * 2 + sy), eye)


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'res', 'cover.png')
    img = Image.new('RGB', (W, H), (0, 0, 0))

    # A starfield behind everything (the middle band shows it).
    rng = random.Random(1991)
    for _ in range(140):
        img.putpixel((rng.randrange(W), rng.randrange(56, 144)), st(rng.choice([(7, 7, 7), (5, 5, 5), (3, 3, 4)])))

    # The games: 4 tiles on top, 4 at the bottom, a raster line between them and the middle.
    for i, (name, left, top) in enumerate(CROPS):
        shot = Image.open(os.path.join(src, name + '.bmp')).convert('RGB').resize((320, 200))
        tile = shot.crop((left, top, left + 80, top + 56))
        tile = Image.eval(tile, lambda v: v)  # (a copy)
        tile.putdata([to_st(tile.getpixel((x, y))) for y in range(56) for x in range(80)])
        x, y = (i % 4) * 80, 0 if i < 4 else 144
        img.paste(tile, (x, y))
        for yy in range(y, y + 56):  # a dark gutter between tiles
            img.putpixel((x, yy), (0, 0, 0))
    rainbow = [st(c) for c in [(7, 0, 0), (7, 3, 0), (7, 6, 0), (3, 7, 0), (0, 7, 3), (0, 5, 7), (2, 2, 7), (6, 0, 7)]]
    for x in range(W):
        img.putpixel((x, 56), rainbow[(x // 8) % 8])
        img.putpixel((x, 143), rainbow[(x // 8 + 4) % 8])

    llama(img, 160, 84)

    # The title, doubled, a raster gradient down its 16 lines (sunset yellow to red), a dark blue drop shadow.
    grad = [st(c) for c in [(7, 7, 4), (7, 7, 2), (7, 7, 0), (7, 6, 0), (7, 5, 0), (7, 5, 0), (7, 4, 0), (7, 3, 0),
                            (7, 3, 0), (7, 2, 0), (7, 2, 1), (7, 1, 1), (6, 1, 2), (6, 0, 2), (5, 0, 3), (4, 0, 3)]]
    title = 'LLAMASOFT ON THE ST'
    text(img, title, (W - len(title) * 16) // 2, 115, 2, lambda r: grad[r], shadow=st((0, 0, 3)))
    sub = "JEFF MINTER'S ATARI ST GAMES"
    text(img, sub, (W - len(sub) * 8) // 2, 134, 1, lambda r: st((4, 6, 7)))

    img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(out)
    print('cover written to', out)


if __name__ == '__main__':
    main()
