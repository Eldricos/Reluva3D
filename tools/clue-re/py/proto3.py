import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys; scale=int(sys.argv[1]); out=sys.argv[2]; sys.argv=[sys.argv[0],os.path.join(GAME, 'Data', 'Classic'),'x']
sys.path.insert(0,'tools'); import proto
from PIL import Image
L=proto.level(proto.files['clue.lvl']); P=proto.parse_recs(L)
items=[]
for pl in P:
    ob=proto.obj(pl['obj'])
    if ob['kind']==11 or pl['b']==4: continue
    st=ob['states']; an=st.get(0) or next(iter(st.values())); A=proto.anim(an)
    x,y=pl['x']*scale//2, pl['y']*scale//2   # scale 2: level units are world units
    items.append((pl['b'] if pl['b']<=1 else 2, x+y, x-y, (x+y)//2, A))
xs=[i[2] for i in items]; ys=[i[3] for i in items]
minx,miny=min(xs)-300,min(ys)-300; W,H=max(xs)-minx+300,max(ys)-miny+100
canvas=Image.new('RGBA',(W,H),(40,40,60,255))
for fl,depth,sx,sy,A in sorted(items,key=lambda i:(i[0],i[1])):
    ox,oy=A['anchor'] if A['iso'] else (-A['w']//2,-A['h'])
    for layer in A['layers']:
        ii,px_,py_=layer[0]
        if ii==0xffff: continue
        im,iox,ioy=proto.image(A['imgs'][ii]); canvas.alpha_composite(im,(sx-minx+ox+px_+iox, sy-miny+oy+py_+ioy))
canvas.convert('RGB').save(out); print(canvas.size)
