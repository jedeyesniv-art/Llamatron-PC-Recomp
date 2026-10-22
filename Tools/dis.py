"""Disassemble the unpacked game image (Private/tron.bin, loaded at 0).
usage: dis.py <hex start> [count=40] [image]"""
import sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000

img = open(sys.argv[3] if len(sys.argv) > 3 else 'Private/tron.bin', 'rb').read()
start, count = int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 40
md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
md.skipdata = True
for i, ins in enumerate(md.disasm(img[start:start + count * 10], start)):
    if i >= count:
        break
    print('%06X  %-20s %-8s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
