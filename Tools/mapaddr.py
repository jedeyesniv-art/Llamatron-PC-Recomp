"""Map code addresses from one build's RAM dump to another's (the Camels' TT addresses came from the STE build's):
the bytes around each address, with longs that look like program addresses ($10000-$FFFFF) as wildcards, searched
in the other dump (smaller windows until one place matches).
usage: mapaddr.py <from.bin> <to.bin> <hex addr> ...   (a data address: <hex code addr>+<hex operand offset>)"""
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()

def pattern(at, before=8, after=40):
    s = at - before
    pat = list(a[s:at + after])
    mask = [True] * len(pat)
    for i in range(0, len(pat) - 3, 2):
        v = int.from_bytes(bytes(pat[i:i + 4]), 'big')
        if 0x10000 <= v < 0x100000:
            for k in range(4): mask[i + k] = False
    return s, pat, mask

def find(at):
    for before, after in ((8, 40), (0, 24), (0, 16), (8, 8), (16, 4)):
        h = find1(at, before, after)
        if len(h) == 1: return h
    return h

def find1(at, before, after):
    s, pat, mask = pattern(at, before, after)
    first = next(i for i, m in enumerate(mask) if m)
    hits = []
    lo, hi = 0x10000, 0x60000
    for p in range(lo, hi):
        ok = True
        for i, (c, m) in enumerate(zip(pat, mask)):
            if m and b[p + i] != c:
                ok = False
                break
        if ok:
            hits.append(p + (at - s))
            if len(hits) > 3: break
    return hits

for arg in sys.argv[3:]:
    if '+' in arg:  # an operand: the long at code + offset in each build
        code, off = arg.split('+')
        code, off = int(code, 16), int(off, 16)
        h = find(code)
        va = int.from_bytes(a[code + off:code + off + 4], 'big')
        vb = [int.from_bytes(b[x + off:x + off + 4], 'big') for x in h]
        print('%-14s %06X -> %s' % (arg, va, ' '.join('%06X' % v for v in vb) or 'NOT FOUND'))
    else:
        at = int(arg, 16)
        h = find(at)
        print('%-14s -> %s' % (arg, ' '.join('%06X' % x for x in h) or 'NOT FOUND'))
