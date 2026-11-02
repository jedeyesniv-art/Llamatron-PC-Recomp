"""Every absolute address a routine reads or writes ($xxxxx.l operands), with the first use: what a draw core
depends on (clips, mode, colour words) before writing a remaster's sandbox set-up (REMASTER.md).
usage: absreads.py <RAM dump or image> <hex start> <hex end>   (run with python -P: Tools/dis.py shadows the dis module)"""
import re, sys, collections
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
img = open(sys.argv[1], 'rb').read()
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000); md.skipdata = True
c = collections.defaultdict(list)
for ins in md.disasm(img[start:end], start):
    for m in re.findall(r'\$([0-9a-f]{5})\.l', ins.op_str):
        c[m].append('%X %s %s' % (ins.address, ins.mnemonic, ins.op_str))
for k in sorted(c): print(k, len(c[k]), c[k][0])
