"""Pasti (.stx) disk image -> raw .st sectors (the readable sectors only; protection tracks and timing are
dropped). usage: stx_to_st.py <image.stx> <out.st>
Format: 16-byte file header "RSY\\0", version, tool, track count; per track a 16-byte record header (record
size, fuzzy byte count, sector count, flags, track length, track number: bit 7 = side, type), then sector
descriptors (16 bytes: data offset, bit position, read time, ID track / side / number / size, CRC, FDC
flags) if flags bit 0, then the fuzzy mask, then the track data the offsets point into."""
import struct
import sys


def stx_sectors(d):
    assert d[:4] == b'RSY\0', 'not a Pasti image'
    tracks = d[10]
    p = 16
    out = {}
    for _ in range(tracks):
        rec, fuzzy, nsec, flags, tlen, tnum, ttype = struct.unpack('<IIHHHBB', d[p:p + 16])
        data = p + 16 + (nsec * 16 if flags & 1 else 0) + fuzzy
        track, side = tnum & 0x7F, tnum >> 7
        if flags & 1:
            for i in range(nsec):
                off, bitpos, rtime, it, isd, num, size, crc, fdc, _ = struct.unpack('<IHHBBBBHBB',
                                                                                    d[p + 16 + i * 16:p + 32 + i * 16])
                n = 128 << (size & 3)
                if fdc & 0x08:  # record not found etc.
                    continue
                out[(track, side, num)] = d[data + off:data + off + n]
        else:  # a standard track without descriptors: sectors 1..nsec in a row
            for i in range(nsec):
                out[(track, side, i + 1)] = d[data + i * 512:data + (i + 1) * 512]
        p += rec
    return out


def to_st(sectors):
    sides = 2 if any(s for (_, s, _) in sectors) else 1
    tracks = max(t for (t, _, _) in sectors) + 1
    spt = max(n for (_, _, n) in sectors if n <= 11)
    img = bytearray(tracks * sides * spt * 512)
    for (t, s, n), b in sectors.items():
        if 1 <= n <= spt and len(b) == 512:
            o = ((t * sides + s) * spt + n - 1) * 512
            img[o:o + 512] = b
    return img, tracks, sides, spt


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    img, tracks, sides, spt = to_st(stx_sectors(d))
    open(sys.argv[2], 'wb').write(img)
    print('%d tracks, %d sides, %d sectors per track -> %d bytes' % (tracks, sides, spt, len(img)))
