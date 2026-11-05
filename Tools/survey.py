"""What moves on a game's screens, frame by frame: reads a --survey=F:T:<file> dump (and optionally the run's log
with --watch=<state> --watch-step=1 lines) and sorts each changed picture into

  scroll   a band of lines that is the previous picture shifted (dx, dy): a scroller, a scrolling landscape
  palette  only colours changed, consistently (a colour cycle, a fade)
  objects  separate small changed areas (sprites, text being typed, particles)
  redraw   most of the screen changed and isn't a shift (a cut, a new page)

Usage: python -P Tools/survey.py <survey.bin> [<log> [<out folder for sample PNGs>]]
Segments = runs of one state value (or 250-frame chunks without a log).
"""
import re
import sys
from collections import Counter, defaultdict

import numpy as np

W, H = 320, 200


def read(path):
    data = open(path, 'rb').read()
    pos, last = 0, None
    while pos < len(data):
        frame, base, nw, changed = np.frombuffer(data, '<i4', 4, pos)
        pos += 16
        writers = np.frombuffer(data, '<i4', 2 * nw, pos).reshape(-1, 2) if nw else np.zeros((0, 2), '<i4')
        pos += 8 * nw
        if changed:
            last = np.frombuffer(data, '<u2', W * H, pos).reshape(H, W)
            pos += 2 * W * H
        yield int(frame), int(base) & 0xFFFFFF, writers, bool(changed), last


def states(log):
    out = {}
    for line in open(log, errors='replace'):
        m = re.search(r'frame (\d+): (@?[0-9A-F]+)=(-?\d+)', line)
        if m:
            out[int(m.group(1))] = int(m.group(3))
    return out


def label(mask):
    """Connected areas of a boolean mask (8-way) on 4 x 4 cells: count and sizes."""
    cells = mask.reshape(H // 4, 4, W // 4, 4).any(axis=(1, 3))
    seen = np.zeros_like(cells)
    sizes = []
    for y, x in zip(*np.nonzero(cells)):
        if seen[y, x]:
            continue
        stack, n = [(y, x)], 0
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            n += 1
            for ny in (cy - 1, cy, cy + 1):
                for nx in (cx - 1, cx, cx + 1):
                    if 0 <= ny < cells.shape[0] and 0 <= nx < cells.shape[1] and cells[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        sizes.append(n)
    return sizes


def bands(prev, cur, diff):
    """Rows with changes, grouped into bands; for each band the shift of prev that explains it best."""
    rows = np.nonzero(diff.any(axis=1))[0]
    if not len(rows):
        return []
    groups, start = [], rows[0]
    for a, b in zip(rows, rows[1:]):
        if b - a > 2:
            groups.append((start, a))
            start = b
    groups.append((start, rows[-1]))
    out = []
    for y0, y1 in groups:
        n = int(diff[y0:y1 + 1].sum())
        best = (0, 0, 0.0)
        if n >= 64:
            shifts = [(dx, 0) for dx in range(-16, 17) if dx] + [(0, dy) for dy in range(-8, 9) if dy]
            for dx, dy in shifts:
                if True:
                    a0, a1 = max(y0, y0 + dy, 0), min(y1, y1 + dy, H - 1)
                    if a1 - a0 < 1:
                        continue
                    xs = slice(max(0, dx), W + min(0, dx))
                    xp = slice(max(0, -dx), W + min(0, -dx))
                    c = cur[a0:a1 + 1, xs]
                    p = prev[a0 - dy:a1 + 1 - dy, xp]
                    d = diff[a0:a1 + 1, xs]
                    ok = ((c == p) & d).sum() / max(1, d.sum())
                    if ok > best[2]:
                        best = (dx, dy, ok)
        out.append((int(y0), int(y1), n, best))
    return out


def classify(prev, cur):
    diff = prev != cur
    n = int(diff.sum())
    if not n:
        return 'same', {}
    pairs = set(zip(prev[diff].tolist(), cur[diff].tolist()))
    froms = Counter(p for p, _ in pairs)
    if max(froms.values()) == 1 and n > 200:
        # every changed colour went to one other colour: a palette change, if unchanged pixels of that colour
        # don't exist (else it's drawing)
        changed_from = {p for p, _ in pairs}
        if not np.isin(prev[~diff], list(changed_from)).any():
            return 'palette', {'n': n}
    info = {'n': n, 'bands': bands(prev, cur, diff)}
    scrolled = sum(b[2] for b in info['bands'] if b[3][2] > 0.8)
    if scrolled > 0.5 * n:
        return 'scroll', info
    if n > 0.4 * W * H:
        return 'redraw', info
    info['blobs'] = label(diff)
    return 'objects', info


def main():
    path = sys.argv[1]
    st = states(sys.argv[2]) if len(sys.argv) > 2 else {}
    out_dir = sys.argv[3] if len(sys.argv) > 3 else None
    segs = []
    prev, cur_state = None, None
    for frame, base, writers, changed, pic in read(path):
        s = st.get(frame, cur_state) if st else (frame // 250) * 250
        if not segs or s != cur_state:
            segs.append({'state': s, 'from': frame, 'to': frame, 'kinds': Counter(), 'scrolls': Counter(),
                         'blobs': [], 'writers': Counter(), 'pictures': 0, 'sample': None, 'bases': set()})
            cur_state = s
        g = segs[-1]
        g['to'] = frame
        g['bases'].add(base)
        for pc, count in writers:
            g['writers'][int(pc)] += int(count)
        if changed and prev is not None:
            g['pictures'] += 1
            kind, info = classify(prev, pic)
            g['kinds'][kind] += 1
            if kind == 'scroll':
                for y0, y1, n, (dx, dy, ok) in info['bands']:
                    if ok > 0.8:
                        g['scrolls'][(y0 // 8 * 8, y1 // 8 * 8 + 7, dx, dy)] += 1
            if kind == 'objects':
                g['blobs'].append(len(info['blobs']))
            if g['sample'] is None and g['pictures'] > 5:
                g['sample'] = (frame, pic.copy())
        if pic is not None:
            prev = pic
    for g in segs:
        frames = g['to'] - g['from'] + 1
        if frames < 10:
            continue
        print(f"state {g['state']}: frames {g['from']}..{g['to']} ({frames}), {g['pictures']} new pictures "
              f"({g['pictures'] * 50 / frames:.1f}/s), screens {len(g['bases'])}")
        if g['kinds']:
            print('   ', ', '.join(f'{k} {v}' for k, v in g['kinds'].most_common()))
        if g['scrolls']:
            print('    scrolls (lines, dx, dy: times):',
                  ', '.join(f'{a}-{b} ({dx},{dy}): {n}' for (a, b, dx, dy), n in g['scrolls'].most_common(6)))
        if g['blobs']:
            b = np.array(g['blobs'])
            print(f'    objects: median {int(np.median(b))} areas, max {b.max()}')
        if g['writers']:
            print('    writers:', ' '.join(f'{pc:06X}:{n}' for pc, n in g['writers'].most_common(10)))
        if out_dir and g['sample']:
            from PIL import Image
            f, pic = g['sample']
            rgb = np.stack([(pic >> 8 & 15) * 17, (pic >> 4 & 15) * 17, (pic & 15) * 17], -1).astype(np.uint8)
            Image.fromarray(rgb).resize((640, 400), Image.NEAREST).save(f'{out_dir}/s{g["state"]}_{f}.png')


main()
