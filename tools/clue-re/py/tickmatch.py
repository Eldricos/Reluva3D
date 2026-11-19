import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import numpy as np, glob, sys
from PIL import Image
tl=np.array(Image.open('tl.pgm')).astype(np.float32).reshape(-1,18,640)
lines=[l.rstrip('\r\n') for l in open(os.path.join(GAME, 'Data', 'Classic', 'Text', 'TickerStrings.txt'),encoding='cp1252')]
X0,X1=60,590
cand=(tl[:,:,X0:X1]>100).astype(np.float32)
# each candidate: column profile of text
cp=cand.sum(1)                      # (N, W)
fs=sorted(glob.glob('vm/tg/*.png'))
out=[]; last=None
for i,f in enumerate(fs):
    a=np.array(Image.open(f)).astype(np.float32)[:,X0:X1]; a[:3]=0; a[15:]=0
    b=(a>110).astype(np.float32); p=b.sum(0)
    if p.sum()<50: continue
    # mask out the right end where the pointer lives sometimes: use full
    best=(-1,None,0)
    # cross-correlate column profiles with shifts (text may have scrolled)
    P=np.fft.rfft(p,2048); C=np.fft.rfft(cp,2048,axis=1)
    cc=np.fft.irfft(C.conj()*P[None,:],2048,axis=1)  # cc[n,s]: shift s of candidate
    k=np.argmax(cc,axis=1); v=cc[np.arange(len(cc)),k]
    norm=v/np.sqrt((cp**2).sum(1)*(p**2).sum()+1e-6)
    n=int(np.argmax(norm))
    if norm[n]<0.9: continue
    if n!=last:
        out.append((500+i/2.0,n,float(norm[n]))); last=n
for t,n,sc in out: print('%6.1f %02d:%04.1f %4d %.3f %s'%(t,int(t//60),t%60,n,sc,lines[n]))
