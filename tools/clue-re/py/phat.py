import numpy as np, sys
rec=np.fromfile(sys.argv[1],dtype=np.int16).astype(np.float32)
sr=8000; N=len(rec)
n=1<<int(np.ceil(np.log2(N+sr*8)))
R=np.fft.rfft(rec,n)
import os
THR=float(os.environ.get("THR","25"))
for name in sys.argv[2:]:
    tpl=np.fromfile('wav8/%s.raw'%name,dtype=np.int16).astype(np.float32)[:sr*4]
    T=np.fft.rfft(tpl[::-1]-tpl.mean(),n)
    X=R*T; X/=np.abs(X)+1e-9
    # keep 150-3500 Hz
    f=np.fft.rfftfreq(n,1/sr); X[(f<150)|(f>3500)]=0
    cc=np.fft.irfft(X,n)[len(tpl)-1:len(tpl)-1+N]
    # score: peak over local robust spread
    L=len(tpl)
    blk=sr*10
    nb=len(cc)//blk
    med=np.array([np.median(np.abs(cc[i*blk:(i+1)*blk])) for i in range(nb)])
    sc=np.abs(cc[:nb*blk]).reshape(nb,blk)/(med[:,None]+1e-12)
    hits=[]
    flat=sc.ravel(); order=np.argsort(-flat)
    taken=np.zeros(len(flat),bool)
    for k in order[:3000]:
        if flat[k]<THR: break
        if taken[max(0,k-L):k+L].any(): continue
        taken[k]=True; hits.append((k/sr,flat[k]))
    hits.sort()
    print('%-22s %3d %s'%(name,len(hits),' '.join('%d:%04.1f(%.0f)'%(t//60,t%60,c) for t,c in hits[:40])),flush=True)
