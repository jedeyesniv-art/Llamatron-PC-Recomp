"""Gridrunner's object table in a RAM dump ($46770, $B4 bytes per object, 15): the active ones.
usage: grid_objects.py <dump.bin> [more dumps...]"""
import struct
import sys

BASE, SIZE, COUNT = 0x46770, 0xB4, 15


def w(m, a):
    return struct.unpack('>h', m[a:a + 2])[0]


for path in sys.argv[1:]:
    m = open(path, 'rb').read()
    print(path)
    for i in range(COUNT):
        o = BASE + i * SIZE
        if not w(m, o):
            continue
        words = ' '.join('%04X' % (w(m, o + k) & 0xFFFF) for k in range(0, 0x40, 2))
        print('  #%2d active %04X x %4d y %4d type %3d hit %04X  | %s' % (
            i, w(m, o) & 0xFFFF, w(m, o + 2), w(m, o + 4), w(m, o + 6), w(m, o + 0x2E) & 0xFFFF, words))
