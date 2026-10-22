"""Draws the program icon (an original pixel llama, not game artwork) -> res/llamatron.ico (+ a preview PNG).
usage: make_icon.py <out dir>"""
import os, sys
from PIL import Image

# 24x24: a llama's head and neck in profile, on a round purple badge.
ART = [
    "........................",
    "........##..##..........",
    "........##..##..........",
    "........##..##..........",
    ".......########.........",
    "......##########........",
    "......###.######........",
    "......#############.....",
    "......##############....",
    "......##############....",
    ".......#####..######....",
    ".......#####............",
    ".......#####............",
    ".......#####............",
    ".......######...........",
    ".......######...........",
    ".......#######..........",
    ".......########.........",
    ".......#########........",
    "......###########.......",
    "......############......",
    "......############......",
    "........................",
    "........................",
]
FUR, EYE, BADGE, RIM = (255, 196, 60, 255), (40, 20, 60, 255), (110, 50, 170, 255), (60, 20, 110, 255)


def draw(size):
    n = len(ART)
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    c = (n - 1) / 2
    for y in range(n):
        for x in range(n):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if d <= c + 0.5:
                im.putpixel((x, y), RIM if d > c - 1 else BADGE)
    for y, row in enumerate(ART):
        for x, ch in enumerate(row):
            if ch == "#":
                im.putpixel((x, y), FUR)
    im.putpixel((9, 6), EYE)  # the eye: the gap in row 6
    return im.resize((size, size), Image.NEAREST)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    big = draw(240).resize((256, 256), Image.NEAREST)
    big.save(os.path.join(out, "llamatron.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (256, 256)])
    big.save(os.path.join(out, "llamatron-icon.png"))
    print("icon written to", out)


if __name__ == "__main__":
    main()
