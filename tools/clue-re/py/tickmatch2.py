import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import numpy as np, glob
from PIL import Image, ImageFilter
tl=np.array(Image.open('tl.pgm')).astype(np.float32).reshape(-1,18,640)
lines=[l.rstrip('\r\n') for l in open(os.path.join(GAME, 'Data', 'Classic', 'Text', 'TickerStrings.txt'),encoding='cp1252')]
X0,X1=60,590
def ext(b, gap=12):
    c=np.where(b.sum(0)>0)[0]
    if not len(c): return None
    groups=[[c[0],c[0],0]]
    for x in c:
        if x-groups[-1][1]>gap: groups.append([x,x,0])
        groups[-1][1]=x; groups[-1][2]+=int(b[:,x].sum())
    g=max(groups,key=lambda g:g[2])
    return (g[0],g[1])
cands=[]
for i in range(len(tl)):
    b=(tl[i,3:15]>100)
    e=ext(b); bb=b[:,e[0]:e[1]+1]; r=np.where(bb.sum(1)>0)[0]
    cands.append((e[1]-e[0]+1, bb[r.min():r.max()+1].astype(np.float32)))
widths=np.array([c[0] for c in cands])
def blur(x):
    k=np.array([0.25,0.5,0.25])
    x=np.apply_along_axis(lambda r: np.convolve(r,k,'same'),1,x)
    return np.apply_along_axis(lambda r: np.convolve(r,k,'same'),0,x)
fs=sorted(glob.glob('vm/tg/*.png'))
last=None; prevsig=None
for i,f in enumerate(fs):
    a=np.array(Image.open(f)).astype(np.float32)[3:15]
    b=(a>110); b[:,:X0]=0; b[:,X1:]=0
    sig=b.sum()
    if prevsig is not None and abs(sig-prevsig)<3 and last is not None: continue  # unchanged
    prevsig=sig
    e=ext(b)
    if e is None: continue
    w=e[1]-e[0]+1
    if w>530: continue  # scrolled round / cursor: skip
    bb=b[:,e[0]:e[1]+1]; r=np.where(bb.sum(1)>0)[0]
    seg=blur(bb[r.min():r.max()+1].astype(np.float32))
    best=(0,-1)
    for n in np.where(np.abs(widths-w)<=6)[0]:
        c=np.array(Image.fromarray((cands[n][1]*255).astype(np.uint8)).resize((seg.shape[1],seg.shape[0]),Image.BILINEAR)).astype(np.float32)/255
        c=blur(c)
        s=(c*seg).sum()/np.sqrt((c**2).sum()*(seg**2).sum()+1e-6)
        if s>best[0]: best=(s,n)
    if best[0]>0.8 and best[1]!=last:
        t=500+i/2.0
        print('%6.1f %02d:%04.1f %4d %.3f %s'%(t,int(t//60),t%60,best[1],best[0],lines[best[1]]),flush=True)
        last=best[1]
