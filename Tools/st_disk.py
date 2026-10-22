"""Atari ST disk images: decode .msa to a raw .st image and extract the files of its FAT12 filesystem.
usage: st_disk.py <image.msa|image.st> <out dir>"""
import os, struct, sys


def msa_to_raw(d):
    magic, spt, sides, first, last = struct.unpack('>5H', d[:10])
    assert magic == 0x0E0F, 'not an MSA image'
    p, out = 10, bytearray()
    for _ in range(first, last + 1):
        for _ in range(sides + 1):
            n = struct.unpack('>H', d[p:p + 2])[0]
            blk = d[p + 2:p + 2 + n]
            p += 2 + n
            if n == spt * 512:
                out += blk
                continue
            i = 0
            while i < len(blk):  # RLE: E5 <byte> <count u16>
                if blk[i] == 0xE5:
                    out += bytes([blk[i + 1]]) * struct.unpack('>H', blk[i + 2:i + 4])[0]
                    i += 4
                else:
                    out.append(blk[i])
                    i += 1
    return bytes(out)


def fat_files(d):
    bps, spc, res, nfats, ndir, _, _, spf = struct.unpack('<HBHBHHBH', d[11:24])
    fat = d[res * bps:(res + spf) * bps]
    root = (res + nfats * spf) * bps
    data = root + ndir * 32
    def next_cluster(c):
        v = fat[c * 3 // 2] | fat[c * 3 // 2 + 1] << 8
        return v >> 4 if c & 1 else v & 0xFFF
    for i in range(ndir):
        e = d[root + i * 32:root + i * 32 + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] & 0x18:
            continue
        name = e[:8].decode('latin1').strip() + '.' + e[8:11].decode('latin1').strip()
        c, size = struct.unpack('<H', e[26:28])[0], struct.unpack('<I', e[28:32])[0]
        body = bytearray()
        while 2 <= c < 0xFF0:
            body += d[data + (c - 2) * spc * bps:data + (c - 1) * spc * bps]
            c = next_cluster(c)
        yield name, bytes(body[:size])


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    if d[:2] == b'\x0e\x0f':
        d = msa_to_raw(d)
    os.makedirs(sys.argv[2], exist_ok=True)
    for name, body in fat_files(d):
        open(os.path.join(sys.argv[2], name), 'wb').write(body)
        print(f'{name:14} {len(body):7}')
