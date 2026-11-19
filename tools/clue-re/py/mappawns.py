# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import cv2, numpy as np, glob, sys, collections, json
ovr=cv2.imread('ovr.png').astype(np.float32)
pawns=[cv2.imread('pawn%d.png'%i).astype(np.float32) for i in range(6)]
pm=[(np.abs(p-60).sum(2)>20) for p in pawns]
pc=[p[m].mean(0) for p,m in zip(pawns,pm)]
board={}
rooms='ABCDEFGHI'
for l in open('board.txt'):
    if l[:3].strip().isdigit():
        y=int(l[:3]);
        for i,c in enumerate(l[4:].rstrip('\n')): board[(9+i,y)]=c
out=[]
for f in sorted(glob.glob(sys.argv[1]+'/*.png')):
    fr=cv2.imread(f).astype(np.float32)
    r=cv2.matchTemplate(fr[:420],ovr,cv2.TM_SQDIFF_NORMED); mn,_,loc,_=cv2.minMaxLoc(r)
    if mn>0.15: continue
    mx,my=loc
    found=[]
    for k in range(6):
        # template match each pawn inside the map
        sub=fr[my:my+407,mx:mx+425]
        rr=cv2.matchTemplate(sub,pawns[k],cv2.TM_SQDIFF,mask=np.dstack([pm[k].astype(np.float32)]*3))
        rr=rr/(pm[k].sum()*3)
        mn2,_,l2,_=cv2.minMaxLoc(rr)
        if mn2<900:
            cx,cy=l2[0]+6,l2[1]+6
            col=9+int((cx-8)//13); row=9+int((cy-15)//13)
            found.append((k,col,row,board.get((col,row),' '),round(mn2)))
    out.append((f,found))
for f,fd in out: print(f.split('/')[-1], fd)
