import sys, cv2, numpy as np
def prep(img):
    g=cv2.cvtColor(img,cv2.COLOR_BGR2GRAY).astype(np.float32)
    # local contrast normalisation removes the lighting
    mu=cv2.GaussianBlur(g,(0,0),12); sd=np.sqrt(cv2.GaussianBlur((g-mu)**2,(0,0),12))+6
    return (g-mu)/sd
rng=np.random.default_rng(1)
houses={n:prep(cv2.imread(n))+rng.normal(0,0.5,cv2.imread(n).shape[:2]).astype(np.float32) for n in sys.argv[1].split(",")}
for f in sys.argv[2:]:
    fr=cv2.imread(f); t=prep(fr)[10:395,10:420]  # left part, avoids the overlays on the right
    best=None
    for n,H in houses.items():
        r=cv2.matchTemplate(H,t,cv2.TM_CCOEFF_NORMED); _,mx,_,loc=cv2.minMaxLoc(r)
        if best is None or mx>best[0]: best=(mx,n,(loc[0]-10,loc[1]-10))
    print(f.split('/')[-1], 'score %.3f'%best[0], best[1], best[2])
