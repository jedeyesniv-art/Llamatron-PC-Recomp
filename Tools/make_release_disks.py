"""Builds the release's Llamasoft-only disk images from the downloaded compilation disks (owner 2026-11-01: ship
no other publishers' games): only the files each Llamasoft game (and its docs reader) opens, found by tracing
Fopen / Pexec. Writes 720 KB .st images (FAT12, files in the root) to <out dir>.
usage: make_release_disks.py <out dir>        (run with python -P: Tools\\dis.py shadows Python's dis)"""
import importlib.util
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
spec = importlib.util.spec_from_file_location('st_disk', os.path.join(ROOT, 'Tools', 'st_disk.py'))
st_disk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(st_disk)

DISKS = [
    # (output image, source image, files to keep)
    ('Andes Attack.st', 'Private/new/andesattack-st/andes attack/med_005.st', ['ANDES.BOY', 'HISCROES.YAK']),
    ('Defender 2.st', 'Private/new/defender2-st/defender2/defender2.msa',
     ['DEFENDR2.PRG', 'DEFENDR2.EXE', 'SAFESNG2.EXE', 'DEFENDER.SET', 'TITLE.4V', 'READ_ME.PP']),
    ('Hardcore.st', 'Private/new/hardcore-st/hardcore/DBUG100E.MSA', ['HARDCORE.PRG', 'DOC100E.PRG', 'EXPLAN.TXT']),
    # Llamasoft's compilation (with Andes, Defender 2 and Gridrunner .YAKs we ship already) - Photon Storm only;
    # the boot sector there is a download site's message.
    ('Photon Storm.st', 'Private/new/photonstorm/photonstorm/PhotonStorm.st', ['PHOTON.YAK', 'PSHIGHS.GNU']),
]


def read_files(path):
    d = open(path, 'rb').read()
    raw = st_disk.msa_to_raw(d) if d[:2] == b'\x0e\x0f' else d
    return {name.rstrip('.'): body for name, body in st_disk.fat_files(raw)}


def write_st(path, files):
    """A double-sided 80-track 9-sector disk: boot sector, 2 FATs of 5 sectors, 112 root entries, 2-sector
    clusters."""
    img = bytearray(737280)
    bs = bytearray(512)
    bs[0:2] = b'\x60\x38'
    bs[8:11] = b'LLA'
    struct.pack_into('<HBHBHHBHHH', bs, 11, 512, 2, 1, 2, 112, 1440, 0xF9, 5, 9, 2)
    img[:512] = bs
    fat = [0xFF9, 0xFFF] + [0] * 708
    root, data = 512 * 11, 512 * 11 + 112 * 32
    cluster = 2
    for i, (name, body) in enumerate(files):
        base, _, ext = name.partition('.')
        n = max(1, (len(body) + 1023) // 1024)
        first = cluster
        for k in range(n):
            fat[cluster] = cluster + 1 if k < n - 1 else 0xFFF
            off = data + (cluster - 2) * 1024
            img[off:off + 1024] = body[k * 1024:(k + 1) * 1024].ljust(1024, b'\0')
            cluster += 1
        entry = base.ljust(8).encode() + ext.ljust(3).encode() + b'\x00' + b'\0' * 10 + struct.pack('<HHHI', 0, 0, first, len(body))
        img[root + i * 32:root + i * 32 + 32] = entry
    fb = bytearray(512 * 5)
    for i in range(0, len(fat) - 1, 2):
        a, b = fat[i], fat[i + 1]
        j = i * 3 // 2
        fb[j:j + 3] = bytes([a & 0xFF, (a >> 8) | ((b & 0xF) << 4), b >> 4])
    img[512:512 + len(fb)] = fb
    img[512 * 6:512 * 6 + len(fb)] = fb
    assert cluster < 710, 'too much for one disk'
    open(path, 'wb').write(img)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, source, keep in DISKS:
        files = read_files(os.path.join(ROOT, source))
        missing = [k for k in keep if k not in files]
        if missing:
            sys.exit('%s: %s not on %s' % (name, ', '.join(missing), source))
        write_st(os.path.join(out, name), [(k, files[k]) for k in keep])
        print('%s: %s' % (name, ' '.join(keep)))


if __name__ == '__main__':
    main()
