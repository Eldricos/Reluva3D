# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import sys,struct,collections,glob,os; sys.path.insert(0,'tools')
from ddrv import load, entries
from lvl import level
from PIL import Image
ROOT=sys.argv[1]; OUT=sys.argv[2]
files={os.path.basename(f).lower():f for f in glob.glob(ROOT+'/**/*',recursive=True) if os.path.isfile(f)}
drives={}
def drive(name):
    if name not in drives:
        d=load(files[name]); drives[name]=(d,{e[4].lower():d[e[2]:e[2]+e[3]] for e in entries(d)})
    return drives[name][1]
def rec(full,ext):
    dr,n=full.split(':',1); return drive(dr.lower()+ext).get(n.lower())
imgcache={}
def image(full):
    if full in imgcache: return imgcache[full]
    r=rec(full,'.res'); ver=struct.unpack('<I',r[4:8])[0]
    if ver==0x200: w,h=struct.unpack('<II',r[16:24]); ds=struct.unpack('<I',r[53:57])[0]; data=r[57:57+ds]; eol=0x80; ox=oy=0
    else: w,h=struct.unpack('<HH',r[12:16]); ox,oy=struct.unpack('<hh',r[36:40]); ds=struct.unpack('<I',r[48:52])[0]; data=r[52:52+ds]; eol=0
    im=Image.new('RGBA',(w,h)); px=im.load(); i=0
    for y in range(h):
        x=0
        while i<len(data):
            c=data[i]; i+=1
            if c==eol: break
            if c&0x80: x+=c&0x7f; continue
            for k in range(c):
                v=data[i]|data[i+1]<<8; i+=2; px[x,y]=((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255); x+=1
    imgcache[full]=(im,ox,oy); return imgcache[full]
def pstr(r,p): n=r[p]; return r[p+1:p+1+n].decode('latin-1'), p+1+n
def anim(full):
    r=rec(full,'.a'); p=6; nm,p=pstr(r,p); a,b,c=r[p:p+3]; p+=3
    nf,ni,w,h=struct.unpack('<HHHH',r[p:p+8]); p+=20; imgs=[]
    for i in range(ni): s,p=pstr(r,p); imgs.append(s)
    layers=[]
    for l in range(c):
        p+=4; layers.append([struct.unpack('<Hhh',r[p+10*f:p+10*f+6]) for f in range(nf)]); p+=10*nf
    anch=(0,0); iso=False
    if r[p:p+4]==b'ISOA': iso=True; anch=struct.unpack('<hh',r[p+8:p+12])
    return dict(w=w,h=h,imgs=imgs,layers=layers,anchor=anch,iso=iso)
def obj(full):
    r=rec(full,'.o'); L,ver=struct.unpack('<HB',r[:3]); kind=r[35]; init=struct.unpack('<I',r[36:40])[0]; n=struct.unpack('<H',r[40:42])[0]
    states={}
    for i in range(n):
        q=42+34*i; sid=struct.unpack('<H',r[q:q+2])[0]; states[sid]=r[q+2:q+34].split(b'\0')[0].decode()
    return dict(kind=kind,init=init,states=states)
def parse_recs(L):
    out=[]
    for r in L['recs'][:L['count']]:
        t=r[0]; p=1; extra=None
        if t&2: extra=struct.unpack('<H',r[p:p+2])[0]; p+=2
        o,x,y,a=struct.unpack('<Hhhh',r[p:p+8]); b=r[p+8]
        out.append(dict(t=t,extra=extra,obj=L['names'][o],x=x,y=y,a=a,b=b))
    return out
if __name__=='__main__':
    L=level(files['clue.lvl']); P=parse_recs(L)
    def proj(x,y): return ((x-y)//2, (x+y)//4)
    items=[]; miss=collections.Counter()
    for pl in P:
        try:
            ob=obj(pl['obj']); st=ob['states']
            an=st.get(ob['init']) or st.get(0) or next(iter(st.values()))
            A=anim(an)
        except Exception as e: miss[repr(e)[:60]]+=1; continue
        sx,sy=proj(pl['x'],pl['y'])
        floor=pl['obj'].split(':')[0].startswith('r_') or 'floor' in pl['obj']
        items.append((0 if floor else 1, pl['x']+pl['y'], sx, sy, A, pl))
    xs=[i[2] for i in items]; ys=[i[3] for i in items]
    minx,maxx,miny,maxy=min(xs)-300,max(xs)+300,min(ys)-300,max(ys)+100
    print('bounds',minx,maxx,miny,maxy, 'missing',miss.most_common(5))
    canvas=Image.new('RGBA',(maxx-minx,maxy-miny),(20,20,30,255))
    for fl,depth,sx,sy,A,pl in sorted(items,key=lambda i:(i[0],i[1])):
        ox,oy=A['anchor'] if A['iso'] else (-A['w']//2,-A['h'])
        for layer in A['layers']:
            ii,px_,py_=layer[0]
            if ii==0xffff: continue
            im,iox,ioy=image(A['imgs'][ii])
            canvas.alpha_composite(im,(sx-minx+ox+px_+iox, sy-miny+oy+py_+ioy))
    canvas.save(OUT); print(canvas.size)
