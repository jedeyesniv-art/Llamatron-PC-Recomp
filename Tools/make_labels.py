"""The launcher's disk labels from the owner's photos of hand-written labels (res/labels/*-photo.jpg):
crop to the drawn box, take out the room lighting (divide by the paper's brightness), make the ink black and the
paper white with soft edges (the ruled lines go), scale to 512 x 392 (the label is 76 x 58 mm).
Writes res/labels/<name>.png (to look at) and src/labels.inc (run-length packed 8-bit grey for the program).
usage: make_labels.py [--preview]"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = os.path.join(os.path.dirname(__file__), '..')
W, H = 512, 392
# The drawn box in each photo, as fractions of the photo (left, top, right, bottom), from the 1500 x 2000 view.
LABELS = [
    ('llamatron', 'llamatron-photo.jpg', (150 / 1500, 558 / 2000, 1268 / 1500, 1408 / 2000), None),
    ('gridrunner', 'gridrunner-photo.jpg', (188 / 1500, 732 / 2000, 1252 / 1500, 1598 / 2000), 'touch_up_g'),
    # No photo yet: a stand-in in a handwriting font until there is one (box = the whole photo when it comes).
    ('tripatron', 'tripatron-photo.jpg', (0, 0, 1, 1), None),
    ('rmc', 'rmc-photo.jpg', (0, 0, 1, 1), None),
    ('defender2', 'defender2-photo.jpg', (0, 0, 1, 1), None),
    ('andes', 'andes-photo.jpg', (0, 0, 1, 1), None),
    ('hardcore', 'hardcore-photo.jpg', (0, 0, 1, 1), None),
]
STAND_IN = {'tripatron': ['Trip-A-Tron'], 'rmc': ['Mutant', 'Camels'], 'defender2': ['Defender 2'],
            'andes': ['Andes', 'Attack'], 'hardcore': ['Hardcore']}


def stand_in(lines):
    """A label written in Windows' Ink Free font (felt-pen look), for a disk without a photographed label: a
    wobbly hand-drawn box, the name in the upper middle, "by Yak" at the bottom right, like the owner's."""
    from PIL import ImageFont
    rng = np.random.default_rng(1992)
    big = Image.new('L', (W * 2, H * 2), 255)
    d = ImageDraw.Draw(big)
    fonts = os.path.join(os.environ.get('WINDIR', 'C:/Windows'), 'Fonts', 'Inkfree.ttf')
    font, small = ImageFont.truetype(fonts, 150), ImageFont.truetype(fonts, 100)
    # The box: each side a few slightly wandering segments, corners overshooting a little.
    x0, y0, x1, y1 = 70, 80, W * 2 - 60, H * 2 - 70
    for (ax, ay), (bx, by) in [((x0 - 10, y0), (x1 + 8, y0 + 6)), ((x1, y0 - 8), (x1 - 6, y1 + 10)),
                               ((x1 + 6, y1), (x0 - 4, y1 - 4)), ((x0, y1 + 8), (x0 + 4, y0 - 12))]:
        pts = [(ax + (bx - ax) * t + rng.normal(0, 2.5), ay + (by - ay) * t + rng.normal(0, 2.5))
               for t in np.linspace(0, 1, 7)]
        d.line(pts, fill=0, width=11, joint='curve')
    heights = [d.textbbox((0, 0), t, font=font, stroke_width=5) for t in lines]
    total = sum(b[3] - b[1] for b in heights) + 40 * (len(lines) - 1)
    y = (H * 2 - total) // 2 - 70
    for t, b in zip(lines, heights):
        d.text(((W * 2 - (b[2] - b[0])) // 2 - b[0], y - b[1]), t, font=font, fill=0, stroke_width=5, stroke_fill=0)
        y += b[3] - b[1] + 40
    bb = d.textbbox((0, 0), 'by Yak', font=small, stroke_width=4)
    d.text((x1 - 50 - (bb[2] - bb[0]) - bb[0], y1 - 40 - (bb[3] - bb[1]) - bb[1]), 'by Yak', font=small, fill=0,
           stroke_width=4, stroke_fill=0)
    big = big.rotate(-2, resample=Image.BICUBIC, fillcolor=255)
    return big.resize((W, H), Image.LANCZOS)


def stroke_mask(size, lines, width):
    m = Image.new('L', size, 0)
    d = ImageDraw.Draw(m)
    for pts in lines:
        d.line(pts, fill=255, width=width, joint='curve')
        r = width // 2
        for x, y in (pts[0], pts[-1]):
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return np.asarray(m, dtype=np.float32) / 255


def touch_up_g(a):
    """The owner's G in "Gridrunner" read as an E. Owner's idea: just join the end of the C's bottom curve up to
    its middle stroke (a G with an overhanging bar), in the same felt pen: solid ink, rough edges.
    Coordinates in the full-resolution crop (2128 x 1732); the G's box starts at (170, 658)."""
    ox, oy = 170, 658
    size = (a.shape[1], a.shape[0])
    shift = lambda pts: [(x + ox, y + oy) for x, y in pts]
    rng = np.random.default_rng(1991)
    ink = stroke_mask(size, [shift([(268, 202), (280, 168), (284, 124)])], 30)
    ink = np.asarray(Image.fromarray((ink * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.5)),
                     dtype=np.float32) / 255
    noise = np.asarray(Image.fromarray((rng.random(ink.shape) * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(1.6)), dtype=np.float32) / 255
    stroke = (ink > 0.5 + 1.6 * (noise - 0.5)) & ~((rng.random(ink.shape) > 0.995) & (ink < 0.9))
    return np.where(stroke, np.minimum(a, 255 * 0.04 * rng.random(ink.shape)), a)


def process(path, box, touch_up=None):
    im = ImageOps.exif_transpose(Image.open(path)).convert('L')  # phone photos: stored sideways + a flag
    w, h = im.size
    im = im.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
    # The paper's brightness: a big blur of the image with the ink filled in (a max filter first removes it).
    small = im.resize((im.width // 8, im.height // 8), Image.BILINEAR)
    paper = small.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(12)).resize(im.size, Image.BILINEAR)
    g = np.asarray(im, dtype=np.float32) / np.maximum(np.asarray(paper, dtype=np.float32), 1)
    # Ink well below the paper (~1.0); ruled lines and paper texture near it.
    v = np.clip((g - 0.40) / (0.68 - 0.40), 0, 1) * 255
    if touch_up:
        v = touch_up(v)
    out = Image.fromarray(v.clip(0, 255).astype(np.uint8)).resize((W, H), Image.LANCZOS)
    return out


def rle(data):
    out = bytearray()
    i = 0
    while i < len(data):
        v, n = data[i], 1
        while i + n < len(data) and data[i + n] == v and n < 255:
            n += 1
        out += bytes((v, n))
        i += n
    return bytes(out)


def main():
    arrays = []
    for name, photo, box, touch_up in LABELS:
        path = os.path.join(ROOT, 'res', 'labels', photo)
        if not os.path.exists(path) and name in STAND_IN:
            img = stand_in(STAND_IN[name])
        else:
            img = process(path, box, globals()[touch_up] if touch_up else None)
        img.save(os.path.join(ROOT, 'res', 'labels', name + '.png'))
        packed = rle(img.tobytes())
        arrays.append((name, packed))
        print(name, img.size, len(packed), 'bytes packed')
    if '--preview' in sys.argv:
        return
    with open(os.path.join(ROOT, 'src', 'labels.inc'), 'w') as f:
        f.write('// Generated by Tools/make_labels.py from the owner\'s hand-written labels: %d x %d 8-bit grey,\n' % (W, H))
        f.write('// run-length packed as (value, count) pairs. Do not edit.\n')
        f.write('constexpr int kLabelW = %d, kLabelH = %d;\n' % (W, H))
        for name, packed in arrays:
            f.write('const unsigned char kLabel_%s[] = {\n' % name)
            for k in range(0, len(packed), 24):
                f.write('  ' + ', '.join('%d' % b for b in packed[k:k + 24]) + ',\n')
            f.write('};\n')
    print('written src/labels.inc')


if __name__ == '__main__':
    main()
