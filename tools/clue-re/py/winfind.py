import cv2, numpy as np, glob, sys, collections
T={n:cv2.imread(n+'.ppm').astype(np.float32) for n in ('escmain','escsave','loser','escpref','escrules')}
res=collections.defaultdict(collections.Counter); ex={}
for d in sys.argv[1:]:
    fs=sorted(glob.glob(d+'/*.jpg'))
    for i,f in enumerate(fs):
        fr=cv2.imread(f).astype(np.float32)[0:40+395,150:290+231]
        for n,t in T.items():
            r=cv2.matchTemplate(fr,t,cv2.TM_CCOEFF_NORMED); _,mx,_,loc=cv2.minMaxLoc(r)
            if mx>0.85:
                k=(loc[0]+150,loc[1]); res[n][k]+=1; ex.setdefault((n,k),(d,i,round(mx,3)))
for n,c in res.items():
    print(n, [(k,v,ex[(n,k)]) for k,v in c.most_common(5)])
