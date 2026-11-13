import numpy as np,sys
rec=np.fromfile('a_gp.raw',dtype=np.int16).astype(np.float64); sr=8000
def best(name,t0,dur=3.0,win=1.0):
    x=np.fromfile('wav8/%s.raw'%name,dtype=np.int16).astype(np.float64)
    out=[]
    for r in np.arange(0.96,1.061,0.004):
        t=np.arange(0,min(len(x)-1,dur*sr*r),r); y=np.interp(t,np.arange(len(x)),x); y-=y.mean()
        a=int((t0-win)*sr); seg=rec[a:a+int((2*win)*sr)+len(y)]
        cc=np.correlate(seg,y,'valid')
        e=np.sqrt(np.convolve(seg**2,np.ones(len(y)),'valid'))
        ncc=cc/(e*np.sqrt((y**2).sum())+1e-9)
        out.append((ncc.max(),r))
    m=max(out); print('%-22s at %6.1f: best rate %.3f (ncc %.2f)'%(name,t0,m[1],m[0]))
for spec in sys.argv[1:]:
    n,t=spec.rsplit('@',1); best(n,float(t))
