# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys; sys.argv=[sys.argv[0],os.path.join(GAME, 'Data', 'Classic'),'x']+sys.argv[1:]
sys.path.insert(0,'tools'); import proto
from PIL import Image
skip_layers=set(int(x) for x in sys.argv[3].split(',') if x) if len(sys.argv)>3 else set()
out=sys.argv[4]
L=proto.level(proto.files['clue.lvl']); P=proto.parse_recs(L)
items=[]
for pl in P:
    ob=proto.obj(pl['obj'])
    if ob['kind']==11 or pl['b'] in skip_layers: continue
    st=ob['states']; an=st.get(ob['init']) or st.get(0) or next(iter(st.values()))
    A=proto.anim(an); sx,sy=((pl['x']-pl['y'])//2,(pl['x']+pl['y'])//4)
    items.append((pl['b'] if pl['b']<=1 else 2, pl['x']+pl['y'], sx, sy, A))
minx,miny=-1300,-150
canvas=Image.new('RGBA',(2900,1500),(40,40,60,255))
for fl,depth,sx,sy,A in sorted(items,key=lambda i:(i[0],i[1])):
    ox,oy=A['anchor'] if A['iso'] else (-A['w']//2,-A['h'])
    for layer in A['layers']:
        ii,px_,py_=layer[0]
        if ii==0xffff: continue
        im,iox,ioy=proto.image(A['imgs'][ii]); canvas.alpha_composite(im,(sx-minx+ox+px_+iox, sy-miny+oy+py_+ioy))
canvas.save(out)
