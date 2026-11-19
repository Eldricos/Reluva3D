# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import numpy as np, glob, sys
from PIL import Image, ImageDraw
v=sys.argv[1]
fs=sorted(glob.glob('tk_%s/*.png'%v))
keep=[]; prev=None
for i,f in enumerate(fs):
    a=np.array(Image.open(f)).astype(np.float32)
    b=(a[2:16,60:560]>110).astype(np.float32)
    if b.sum()<40: continue
    if prev is not None:
        p=b.sum(0); q=prev.sum(0)
        best=max(np.dot(np.roll(p,s),q) for s in range(-60,61))/np.sqrt(np.dot(p,p)*np.dot(q,q))
        if best>0.93: continue
    prev=b; keep.append((i,f))
print(v,len(keep))
rows=64
for k in range(0,len(keep),rows):
    sh=Image.new('L',(760,18*rows),0); d=ImageDraw.Draw(sh)
    for j,(i,f) in enumerate(keep[k:k+rows]):
        t=i/2; sh.paste(Image.open(f).crop((40,0,640,18)),(160,18*j)); d.text((4,18*j+3),'%02d:%04.1f'%(t//60,t%60),fill=255)
    sh.resize((1520,36*rows)).save('ts_%s_%d.png'%(v,k//rows))
