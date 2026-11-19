import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys,cv2,numpy as np; sys.path.insert(0,'tools')
frame=sys.argv[1]; xmax=int(sys.argv[2]); names=sys.argv[3:]
sys.argv=['x',os.path.join(GAME, 'Data', 'Classic'),'y']
import proto
L=proto.level(proto.files['clue.lvl']); P=proto.parse_recs(L)
fr=cv2.imread(frame).astype(np.float32)
def grad(a):
    g=cv2.cvtColor(a,cv2.COLOR_BGR2GRAY); return cv2.magnitude(cv2.Sobel(g,cv2.CV_32F,1,0),cv2.Sobel(g,cv2.CV_32F,0,1))
G=grad(fr)[:410,:xmax]
for name in names:
    ob=proto.obj(name); A=proto.anim(ob['states'][0]); ii,px,py=A['layers'][0][0]
    im,ox,oy=proto.image(A['imgs'][ii]); sp=np.array(im.convert('RGBA')).astype(np.float32)
    t=grad(sp[:,:,2::-1].copy()); mask=(sp[:,:,3]>0).astype(np.float32)
    r=cv2.matchTemplate(G,t,cv2.TM_CCORR_NORMED,mask=mask); r[~np.isfinite(r)]=0; _,mx,_,loc=cv2.minMaxLoc(r)
    ax,ay=A['anchor']; places=[(p['x'],p['y']) for p in P if p['obj']==name]
    print(f"{name:28s} image at {loc} score {mx:.3f} anchor on screen ({loc[0]-ax-px},{loc[1]-ay-py}) level places {places}")
