import sys, cv2, numpy as np
house=cv2.imread(sys.argv[1]); H=cv2.cvtColor(house,cv2.COLOR_BGR2GRAY).astype(np.float32)
def edges(g): 
    gx=cv2.Sobel(g,cv2.CV_32F,1,0,ksize=3); gy=cv2.Sobel(g,cv2.CV_32F,0,1,ksize=3)
    m=cv2.magnitude(gx,gy); return m/(cv2.GaussianBlur(m,(31,31),0)+8)
HE=edges(H)
for f in sys.argv[2:]:
    fr=cv2.imread(f); g=cv2.cvtColor(fr,cv2.COLOR_BGR2GRAY).astype(np.float32)[8:400,8:632]
    r=cv2.matchTemplate(HE,edges(g),cv2.TM_CCOEFF_NORMED); _,mx,_,loc=cv2.minMaxLoc(r)
    print(f, 'score %.3f'%mx, 'frame top-left in house image', (loc[0]-8,loc[1]-8))
