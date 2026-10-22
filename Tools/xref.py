"""Find instructions referencing an address (absolute operand) in the unpacked image (linear sweep).
usage: xref.py <hex addr>[,<hex addr>...] [image]"""
import sys, re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000

img = open(sys.argv[2] if len(sys.argv) > 2 else 'Private/tron.bin', 'rb').read()
want = {int(a, 16) for a in sys.argv[1].split(',')}
md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
md.skipdata = True
for ins in md.disasm(img, 0):
    for m in re.findall(r'\$([0-9a-f]+)', ins.op_str):
        if int(m, 16) in want:
            print('%06X  %-8s %s' % (ins.address, ins.mnemonic, ins.op_str))
            break
