# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import numpy as np, glob, os, sys
rec=np.fromfile(sys.argv[1],dtype=np.int16).astype(np.float32)
names=sys.argv[2:]
N=len(rec); sr=8000
# local energy for normalisation
def find(tpl, thr):
    tpl=tpl-tpl.mean(); L=len(tpl)
    n=1<<int(np.ceil(np.log2(N+L)))
    R=np.fft.rfft(rec,n); T=np.fft.rfft(tpl[::-1],n)
    cc=np.fft.irfft(R*T,n)[L-1:L-1+N-L+1]
    c2=np.concatenate([[0],np.cumsum(rec.astype(np.float64)**2)])
    e=np.sqrt(np.maximum(c2[L:L+len(cc)]-c2[:len(cc)],1e-6))
    tn=np.sqrt((tpl**2).sum())
    ncc=cc/(e*tn+1e-6)
    ncc[e<0.2*tn]=0  # too quiet there to hold it
    hits=[]; i=0
    order=np.argsort(-ncc)
    taken=np.zeros(len(ncc),bool)
    for k in order[:5000]:
        if ncc[k]<thr: break
        if taken[max(0,k-L):k+L].any(): continue
        taken[k]=True; hits.append((k/sr, float(ncc[k])))
    return sorted(hits)
for name in names:
    tpl=np.fromfile('wav8/%s.raw'%name,dtype=np.int16).astype(np.float32)
    tpl=tpl[:min(len(tpl),sr*3)]  # first 3 s
    h=find(tpl,float(os.environ.get("THR","0.5")))
    print('%-26s %3d  %s'%(name,len(h),' '.join('%d:%04.1f(%.2f)'%(t//60,t%60,c) for t,c in h[:40])))
