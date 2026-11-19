# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import pickle,re,sys
ins=pickle.load(open(os.path.join(WORK, 'insns.pkl'),'rb'))
d=open(os.path.join(GAME, 'Clue.exe'),'rb').read()
names={}
for m in re.finditer(rb'UI:[A-Za-z_%0-9]+',d): names[m.start()+0x401c00]=m.group().decode()
idx={}
for k,(a,m,o) in enumerate(ins):
    for v in re.findall(r'0x45f[0-9a-f]{3}',o):
        v=int(v,16)
        if v in names: idx.setdefault(v,[]).append(k)
for v,ks in sorted(idx.items()):
    for k in ks:
        nums=[]
        for j in range(k-3,k+40):
            a,m,o=ins[j]
            if m=='push' and re.fullmatch(r'0x[0-9a-f]{1,3}|[0-9]',o): nums.append(int(o,16) if o.startswith('0x') else int(o))
            if m=='call' and 'ptr' not in o and len(nums)>=2 and j>k: break
        print(names[v], hex(ins[k][0]), nums[:8])
