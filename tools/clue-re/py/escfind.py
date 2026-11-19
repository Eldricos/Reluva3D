# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import cv2, numpy as np, glob, sys
t=cv2.imread('esclogo.png').astype(np.float32)
for n in sys.argv[1:]:
    fs=sorted(glob.glob('esc_%s/*.jpg'%n)); hits=[]
    for i,f in enumerate(fs):
        fr=cv2.imread(f).astype(np.float32)[0:80,150:500]
        r=cv2.matchTemplate(fr,t,cv2.TM_CCOEFF_NORMED); _,mx,_,_=cv2.minMaxLoc(r)
        if mx>0.8: hits.append(i)
    runs=[]
    for s in hits:
        if runs and s-runs[-1][1]<=2: runs[-1][1]=s
        else: runs.append([s,s])
    print(n, ['%d:%02d-%d:%02d'%(a//60,a%60,b//60,b%60) for a,b in runs], flush=True)
