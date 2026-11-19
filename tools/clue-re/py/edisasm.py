# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Disassembles !EagleM.dll's code into WORK/eagle.pkl for edis.py: a linear
# sweep that starts again past each byte Capstone can't read (data among the
# code would otherwise end it early).
import os, struct, pickle, capstone
GAME = os.environ.get('CLUE_GAME', 'game')
WORK = os.environ.get('CLUE_WORK', '.')
d = open(os.path.join(GAME, '!EagleM.dll'), 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<8sIIII', d, pe + 24 + opt + 40 * i) for i in range(n)]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
ins = []
name, vsize, va, rsize, raw = secs[0]
code = d[raw:raw + vsize]
off = 0
while off < len(code):
    for i in md.disasm(code[off:], base + va + off):
        ins.append((i.address, i.mnemonic, i.op_str))
        off = i.address - (base + va) + i.size
    if off < len(code):
        off += 1
pickle.dump((base, secs, ins), open(os.path.join(WORK, 'eagle.pkl'), 'wb'))
print(len(ins), 'instructions')
