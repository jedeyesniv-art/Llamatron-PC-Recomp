"""Unpack TRON1MEG.PRG (Llamatron).

The PRG is a small self-extractor: it moves itself to the top of its text, unpacks an LZ77 stream to its own
text start (basepage + $100), applies the game's standard GEMDOS relocation table (stored after the unpacked
text) and clears $965 words of BSS. This tool does the same in Python, without relocating, and writes:
  <out>.bin    the unpacked text, as if loaded at address 0
  <out>.reloc  the offsets of the longs to relocate (u32 big-endian each)
usage: depack.py <TRON1MEG.PRG> <out prefix>"""
import struct, sys

TEXT_SIZE = 0x4CFA6   # unpacked text size (immediates in the depacker at $4E204)


STREAM = 0x26         # the packed stream follows the 38-byte mover at the start of the text


def packed_text(prg):
    tl = struct.unpack('>I', prg[2:6])[0]
    text = prg[28:28 + tl]
    # sanity check: the depacker's first instruction, 'movea.l 4(a7),a3; lea $100(a3),a3' (basepage + $100)
    assert text.find(bytes.fromhex('266f000447eb0100')) > 0, 'depacker not found'
    return text


def unpack(src, pos):
    out = bytearray()
    while True:
        b = src[pos]; pos += 1
        if b >= 0x80:                       # match: 3..34 bytes, distance up to 2 KB
            n = (b & 0x1F) + 3
            d = ((b << 3) & 0xFF00) | src[pos]; pos += 1
            p = len(out) - 0x800 + d
            for _ in range(n):
                out.append(out[p]); p += 1
        elif b == 0:
            return bytes(out), pos
        else:                               # literal run
            out += src[pos:pos + b]; pos += b


def relocs(table):
    off = struct.unpack('>I', table[:4])[0]
    if not off:
        return []
    res, i = [off], 4
    while True:
        x = table[i]; i += 1
        if x == 0:
            return res
        if x == 1:
            off += 254
        else:
            off += x
            res.append(off)


def main():
    prg = open(sys.argv[1], 'rb').read()
    data, _ = unpack(packed_text(prg), STREAM)
    assert len(data) > TEXT_SIZE, 'unpacked data too short (%X)' % len(data)
    image, table = data[:TEXT_SIZE], data[TEXT_SIZE:]
    rel = relocs(table)
    open(sys.argv[2] + '.bin', 'wb').write(image)
    open(sys.argv[2] + '.reloc', 'wb').write(b''.join(struct.pack('>I', r) for r in rel))
    print('text $%X bytes, %d relocations' % (len(image), len(rel)))


if __name__ == '__main__':
    main()
