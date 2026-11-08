"""Steam library art for "Llamasoft on the ST" (owner 2026-11-07), in the cover's style (Tools/make_cover.py): the
title in the program's 8 x 8 font with the Llamasoft raster gradient, the program's llama on its badge, rainbow raster
lines, and tiles of the games as the app shows them (screenshots through each game's own CRT++ look).
Writes res/steam/: capsule.png 600 x 900 (library portrait), wide.png 920 x 430 (horizontal capsule), hero.png
1920 x 620 (the banner behind the game's page), logo.png 1280 x 360 (transparent, over the hero). The settings
disk's ADD TO STEAM copies them into Steam's grid folder (src/steam.cpp).
The screenshots (not in git, logs/steamshots): the owner's captures in own/ and windowed runs of ours (--window
--window-size=1920x1200 --rshot=N:<name>_N.bmp); PICKS names the ones used and the part of each picture shown.
usage: python -P Tools/make_steam_art.py <folder of screenshots> [<out folder>]"""
import importlib.util
import os
import random
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
spec = importlib.util.spec_from_file_location('make_cover', os.path.join(ROOT, 'Tools', 'make_cover.py'))
cover = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cover)
st, font = cover.st, cover.font()

# Our own 1920 x 1200 captures: the picture is PAL aspect (320 x 217 in square units), centred, full height.
SHOT_W, SHOT_H = 1920, 1200
OWN_PIC = ((SHOT_W - SHOT_H * 320 / 217) / 2, 0, (SHOT_W + SHOT_H * 320 / 217) / 2, SHOT_H)

# name: (file in the shots folder, the picture's rectangle in it (None: the whole image), centre x, centre y, zoom).
# Centre in the picture's 0..1 coordinates; zoom 1 = the picture's whole height (below 1: black around it), 'fit' =
# its whole width.
# own/ = the owner's captures (2026-11-07: title screens through the app's CRT++, from their Videos\Captures;
# Grid / Revenge / Hardcore are 4K frames with the side bars, Grid's pad hint line below y 1830); ours (a windowed
# run's 1920 x 1200) for Defender II's title.
PICKS = {
    'llamatitle': ('own/Llamatron.png', None, 0.5, 0.5, 'fit'),
    'gridrunner': ('own/Grid.png', (328, 0, 3511, 1825), 0.5, 0.5, 1.07),
    'tripatron': ('own/Trip.png', None, 0.5, 0.52, 1.45),
    'camels': ('own/Revenge.png', (328, 0, 3502, 2160), 0.5, 0.5, 1.0),
    'andes': ('own/Andes.png', None, 0.5, 0.48, 1.12),
    'defender2': ('defender2_1550.bmp', OWN_PIC, 0.5, 0.42, 1.12),
    'hardcore': ('own/Hardcore.png', (328, 0, 3511, 2160), 0.5, 0.5, 1.0),
    'photonstorm': ('own/photon.png', None, 0.5, 0.45, 'fit'),
}

RAINBOW = [st(c) for c in [(7, 0, 0), (7, 3, 0), (7, 6, 0), (3, 7, 0), (0, 7, 3), (0, 5, 7), (2, 2, 7), (6, 0, 7)]]
GRAD = [st(c) for c in [(7, 7, 4), (7, 7, 2), (7, 7, 0), (7, 6, 0), (7, 5, 0), (7, 5, 0), (7, 4, 0), (7, 3, 0),
                        (7, 3, 0), (7, 2, 0), (7, 2, 1), (7, 1, 1), (6, 1, 2), (6, 0, 2), (5, 0, 3), (4, 0, 3)]]
TITLE, SUB = 'LLAMASOFT ON THE ST', "JEFF MINTER'S ATARI ST GAMES"


def tile(shots, name, w, h, fit=False):
    """A game's tile: part of its picture, w x h, cropped to that shape around the pick's centre (inside the picture
    where it fits; centred, with black around, where it doesn't)."""
    path, rect, cx, cy, zoom = PICKS[name]
    img = Image.open(os.path.join(shots, path)).convert('RGB')
    if rect:  # only the picture: anything beyond it (bars, the pad hint line) comes out black
        img = img.crop(tuple(int(v) for v in rect))
    x0, y0, x1, y1 = 0, 0, img.width, img.height
    pw, ph = x1 - x0, y1 - y0
    if fit or zoom == 'fit':  # the picture's whole width (black above and below if the tile is taller)
        cw = pw
        ch = cw * h / w
    else:
        ch = ph / zoom
        cw = ch * w / h
    x = x0 + cx * pw - cw / 2
    y = y0 + cy * ph - ch / 2
    x = min(max(x, x0), x1 - cw) if cw <= pw else x0 + (pw - cw) / 2
    y = min(max(y, y0), y1 - ch) if ch <= ph else y0 + (ph - ch) / 2
    if x < 0 or y < 0 or x + cw > img.width or y + ch > img.height:  # (black beyond the image)
        pad = Image.new('RGB', (img.width + 2 * int(cw), img.height + 2 * int(ch)), (0, 0, 0))
        pad.paste(img, (int(cw), int(ch)))
        img, x, y = pad, x + int(cw), y + int(ch)
    return img.resize((w, h), Image.LANCZOS, box=(x, y, x + cw, y + ch))


def text_image(s, fp, rows, shadow=None):
    """s in the 8 x 8 font, fp output pixels a font pixel, transparent. rows(f) = the colour at f (0..1 down the
    glyphs: a raster gradient); shadow: a drop shadow half a font pixel down and right."""
    off = max(1, fp // 2)
    img = Image.new('RGBA', (len(s) * 8 * fp + off, 8 * fp + off), (0, 0, 0, 0))
    for layer in ('shadow', 'text'):
        if layer == 'shadow' and not shadow:
            continue
        for i, ch in enumerate(s):
            glyph = font(ch)
            for gy in range(8):
                for gx in range(8):
                    if not glyph[gy] >> gx & 1:
                        continue
                    x, y = (i * 8 + gx) * fp, gy * fp
                    if layer == 'shadow':
                        img.paste(shadow + (255,), (x + off, y + off, x + off + fp, y + off + fp))
                        continue
                    for sy in range(fp):
                        img.paste(rows((y + sy) / (8 * fp)) + (255,), (x, y + sy, x + fp, y + sy + 1))
    return img


def title_image(fp):
    """The title with the cover's sunset gradient down its lines and a dark blue shadow."""
    return text_image(TITLE, fp, lambda f: GRAD[min(15, int(f * 16))], st((0, 0, 3)))


def sub_image(fp):
    return text_image(SUB, fp, lambda f: st((4, 6, 7)), st((0, 0, 2)))


def llama_image():
    """make_icon.py's llama on its badge (make_cover.py's), transparent around it; 1 pixel = an ST pixel."""
    canvas = Image.new('RGB', (64, 64), (1, 2, 3))
    cover.llama(canvas, 32, 32)
    out = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
    for y in range(64):
        for x in range(64):
            p = canvas.getpixel((x, y))
            if p != (1, 2, 3):
                out.putpixel((x, y), p + (255,))
    return out.crop(out.getbbox())


def paste_px(dst, src, x, y, scale=1):
    """src scaled up by a whole number (square ST pixels), alpha kept."""
    big = src.resize((src.width * scale, src.height * scale), Image.NEAREST)
    if dst.mode == 'RGBA':
        dst.alpha_composite(big, (int(x), int(y)))
    else:
        dst.paste(big, (int(x), int(y)), big)


def stars(img, box, count, seed):
    rng = random.Random(seed)
    x0, y0, x1, y1 = box
    for _ in range(count):
        c = st(rng.choice([(7, 7, 7), (5, 5, 5), (3, 3, 4)]))
        x, y, s = rng.randrange(x0, x1), rng.randrange(y0, y1), rng.choice([2, 3, 3, 4])
        img.paste(c, (x, y, x + s, y + s))


def raster_line(img, y, height, px, phase=0):
    for x in range(0, img.width, px * 8):
        img.paste(RAINBOW[(x // (px * 8) + phase) % 8], (x, y, x + px * 8, y + height))


def capsule(shots):
    """Library portrait 600 x 900: the llama and the title on stars above, six games below in a 2 x 3 grid."""
    img = Image.new('RGB', (600, 900), (0, 0, 0))
    stars(img, (0, 0, 600, 290), 70, 1991)
    llama, title, sub = llama_image(), title_image(3), sub_image(2)
    paste_px(img, llama, 300 - llama.width * 3 // 2, 30, 3)
    y = 30 + llama.height * 3 + 22
    paste_px(img, title, 300 - title.width // 2, y)
    paste_px(img, sub, 300 - sub.width // 2, y + title.height + 14)
    raster_line(img, 290, 6, 2)
    names = ['llamatitle', 'camels', 'gridrunner', 'andes', 'defender2', 'hardcore']
    for i, n in enumerate(names):
        x, y = (i % 2) * 301, 296 + (i // 2) * 202
        img.paste(tile(shots, n, 299, 200), (x, y + 2))
    return img


def wide(shots):
    """Horizontal capsule 920 x 430: the llama and the title on stars on top, four games below."""
    img = Image.new('RGB', (920, 430), (0, 0, 0))
    stars(img, (0, 0, 920, 190), 60, 1988)
    llama, title, sub = llama_image(), title_image(4), sub_image(2)
    block_w = llama.width * 3 + 28 + title.width
    x0 = (920 - block_w) // 2
    paste_px(img, llama, x0, (190 - llama.height * 3) // 2, 3)
    tx = x0 + llama.width * 3 + 28
    ty = (190 - title.height - 16 - sub.height) // 2
    paste_px(img, title, tx, ty)
    paste_px(img, sub, tx + (title.width - sub.width) // 2, ty + title.height + 16)
    raster_line(img, 190, 6, 2, 3)
    names = ['llamatitle', 'camels', 'gridrunner', 'photonstorm']
    for i, n in enumerate(names):
        img.paste(tile(shots, n, 228, 232, fit=True), (i * 230 + (1 if i else 0), 198))
    return img


def hero(shots):
    """The page banner 1920 x 620: eight games in two rows of four (Steam puts the logo over its lower left)."""
    img = Image.new('RGB', (1920, 620), (0, 0, 0))
    names = ['llamatitle', 'gridrunner', 'tripatron', 'camels', 'andes', 'defender2', 'hardcore', 'photonstorm']
    for i, n in enumerate(names):
        x, y = (i % 4) * 480, (i // 4) * 312
        img.paste(tile(shots, n, 476, 306), (x + 2, y + (0 if i < 4 else 2)))
    raster_line(img, 306, 6, 3)
    return img


def logo():
    """Transparent: the llama and the title, for over the hero (Steam scales it)."""
    img = Image.new('RGBA', (1280, 400), (0, 0, 0, 0))
    llama, title, sub = llama_image(), title_image(8), sub_image(4)
    paste_px(img, llama, (1280 - llama.width * 5) // 2, 0, 5)
    y = llama.height * 5 + 20
    paste_px(img, title, (1280 - title.width) // 2, y)
    paste_px(img, sub, (1280 - sub.width) // 2, y + title.height + 18)
    return img.crop(img.getbbox())


def readme_cover(shots):
    """The README's cover 1280 x 800 (res/cover.png, 2.0): four games above and four below, the llama, the title and
    "2.0 - REMASTER" on stars between them, rainbow raster lines either side."""
    img = Image.new('RGB', (1280, 800), (0, 0, 0))
    th = 228  # each tile row
    names = ['llamatitle', 'gridrunner', 'tripatron', 'camels', 'andes', 'defender2', 'hardcore', 'photonstorm']
    for i, n in enumerate(names):
        x, y = (i % 4) * 320, 0 if i < 4 else 800 - th
        img.paste(tile(shots, n, 318, th, fit=(n not in ('tripatron', 'defender2'))), (x + (1 if i % 4 else 0), y))
    band = (th + 6, 800 - th - 6)
    stars(img, (0, band[0], 1280, band[1]), 110, 2112)
    raster_line(img, th, 6, 3)
    raster_line(img, 800 - th - 6, 6, 3, 4)
    llama, title, sub = llama_image(), title_image(5), sub_image(3)
    remaster = text_image('2.0 - REMASTER', 4, lambda f: RAINBOW[min(7, int(f * 8))], st((0, 0, 2)))
    lw, lh = llama.width * 4, llama.height * 4
    gap = 44
    text_w = max(title.width, remaster.width, sub.width)
    x0 = (1280 - lw - gap - text_w) // 2
    mid = (band[0] + band[1]) // 2
    paste_px(img, llama, x0, mid - lh // 2, 4)
    tx = x0 + lw + gap
    block_h = title.height + 22 + remaster.height + 22 + sub.height
    ty = mid - block_h // 2
    paste_px(img, title, tx + (text_w - title.width) // 2, ty)
    ty += title.height + 22
    paste_px(img, remaster, tx + (text_w - remaster.width) // 2, ty)
    ty += remaster.height + 22
    paste_px(img, sub, tx + (text_w - sub.width) // 2, ty)
    return img


def main():
    shots = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == '--cover':  # the README's cover only
        img = readme_cover(shots)
        path = os.path.join(ROOT, 'res', 'cover.png')
        img.save(path, optimize=True)
        print(f'{path} {img.size[0]} x {img.size[1]}')
        return
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'res', 'steam')
    os.makedirs(out, exist_ok=True)
    for name, make in (('capsule', lambda: capsule(shots)), ('wide', lambda: wide(shots)),
                       ('hero', lambda: hero(shots)), ('logo', logo)):
        img = make()
        img.save(os.path.join(out, name + '.png'), optimize=True)
        print(f'{name}.png {img.size[0]} x {img.size[1]}')


if __name__ == '__main__':
    main()
