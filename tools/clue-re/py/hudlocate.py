# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys,cv2,numpy as np; sys.path.insert(0,'tools')
frame=sys.argv[1]; names=sys.argv[2:]; sys.argv=['x',os.path.join(GAME, 'Data', 'Classic'),'y']
import proto
fr=cv2.imread(frame).astype(np.float32)
for n in names:
    try:
        A=proto.anim(n)
    except Exception as e: print(n,'no anim'); continue
    best=None
    for fi,(ii,px,py) in enumerate(A['layers'][0][:6]):
        if ii==0xffff: continue
        im,ox,oy=proto.image(A['imgs'][ii]); sp=np.array(im.convert('RGBA')).astype(np.float32)
        if sp.shape[0]>fr.shape[0] or sp.shape[1]>fr.shape[1]: continue
        t=sp[:,:,2::-1].copy(); m=(sp[:,:,3]>0).astype(np.float32)
        r=cv2.matchTemplate(fr,t,cv2.TM_SQDIFF,mask=np.dstack([m]*3)); r[~np.isfinite(r)]=1e18
        mn,_,loc,_=cv2.minMaxLoc(r); err=mn/(m.sum()*3)
        if best is None or err<best[0]: best=(err,fi,loc,im.size,(px+ox,py+oy))
    if best: print(f"{n:20s} frame {best[1]} image top-left {best[2]} size {best[3]} part offset {best[4]} mean sq err {best[0]:.1f}")
