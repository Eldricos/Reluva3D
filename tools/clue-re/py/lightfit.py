# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

from PIL import Image
import numpy as np
data=[]
for t,who in ((42,'character:Oscarlet'),(72,'character:Omustard')):
    ids=np.fromfile('vm2h/id%d'%t,dtype=np.int32).reshape(480,640)
    a=np.array(Image.open('vm2h/%d.png'%t).convert('RGB')).astype(np.float32).sum(2)
    b=np.array(Image.open('vm2h/id%d.ppm'%t).convert('RGB')).astype(np.float32).sum(2)
    lamps=[]; sus=None
    for l in open('vm2h/id%d.txt'%t):
        p=l.split(); n=p[1]; rx,ry,rw,rh,ax,ay=map(int,p[8:14])
        if ('scnc' in n or 'lite' in n or 'firepl' in n) and -100<rx<740 and -100<ry<500: lamps.append((rx+rw/2, ry+rh/2))
        if n==who: sus=(ax,ay)
    ys,xs=np.mgrid[20:400:3,20:620:3]
    m=(ids[ys,xs]>=0)&(b[ys,xs]>120)
    r=(a[ys,xs]/np.maximum(b[ys,xs],1))[m]; X=xs[m].astype(float); Y=ys[m].astype(float)
    L=np.array(lamps); dl=np.min(np.hypot(X[:,None]-L[None,:,0],Y[:,None]-L[None,:,1]),axis=1)
    data.append((r,X,Y,dl,sus))
    print(t,len(lamps),'lamps, suspect',sus, len(r))
def model(p, X,Y,dl,sus):
    amb,lc,lf,lp,sc,sf,sp,h=p
    L=lambda d,c,f,pk: amb+(pk-amb)*np.clip(1-(d-c)/f,0,1)
    ds=np.hypot(X-sus[0],Y-(sus[1]-h))
    return np.maximum(L(dl,lc,lf,lp),L(ds,sc,sf,sp))
def err(p): return sum(np.median(np.abs(model(p,X,Y,dl,sus)-r)) for r,X,Y,dl,sus in data)
best=None
for amb in (0.2,0.25,0.3):
  for lc in (0,10,20):
    for lf in (20,40,60,90):
      for lp in (0.8,1.0,1.2):
        for sc in (20,40,60):
          for sf in (100,150,200,250):
            for sp in (1.0,1.2):
              for h in (0,30,60):
                p=(amb,lc,lf,lp,sc,sf,sp,h); e=err(p)
                if best is None or e<best[0]: best=(e,p)
print('best',best)
p=list(best[1]); p[3]=p[0]; print('without lamps',err(tuple(p)))
p=list(best[1]); p[6]=p[0]; print('without the suspect',err(tuple(p)))
print('flat ambient', sum(np.median(np.abs(best[1][0]-r)) for r,*_ in data))
