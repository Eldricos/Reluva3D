# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import sys,glob,collections,struct; sys.path.insert(0,'tools')
from ddrv import load, entries
def pstr(r,p):
    n=r[p]; return r[p+1:p+1+n], p+1+n
def parse(r):
    assert r[:4]==b'ANIM'
    ver=struct.unpack('<H',r[4:6])[0]; p=6
    name,p=pstr(r,p)
    a,b,c=r[p],r[p+1],r[p+2]; p+=3
    nf,ni,w,h,u1,u2,ftb=struct.unpack('<HHHHIII',r[p:p+20]); p+=20
    imgs=[]
    for i in range(ni):
        s,p=pstr(r,p); imgs.append(s.decode('latin-1'))
    pre=r[p:p+4]; p+=4
    frames=[r[p+10*i:p+10*i+10] for i in range(nf)]; p+=10*nf
    blocks={}
    while p+8<=len(r) and r[p:p+4].isalpha() and r[p:p+4]!=b'ANIM':
        tag=r[p:p+4]; sz=struct.unpack('<I',r[p+4:p+8])[0]; blocks[tag]=r[p+8:p+8+sz]; p+=8+sz
    return dict(ver=ver,name=name,abc=(a,b,c),nf=nf,ni=ni,w=w,h=h,u1=u1,u2=u2,ftb=ftb,imgs=imgs,pre=pre,frames=frames,blocks=blocks,end=p)
if __name__=='__main__':
    st=collections.Counter(); ex=collections.defaultdict(list)
    for f in sorted(glob.glob('clue_dl/ClueInstall/**/*.a',recursive=True)+glob.glob('clue_dl/ClueMABM/clue22/*.a')):
        d=load(f)
        for fl,ft,off,size,name,raw in entries(d):
            r=d[off:off+size]
            try: A=parse(r)
            except Exception as e: st['fail']+=1; ex['fail'].append((f,name,repr(e))); continue
            st[('ver',A['ver'])]+=1; st[('end-size', A['end']-size)]+=1
            st[('ftb ok', A['ftb']==4+10*A['nf'])]+=1; st[('u1',A['u1'])]+=1; st[('u2',A['u2'])]+=1
            st[('pre',A['pre'].hex())]+=1; st[('blocks',tuple(A['blocks']))]+=1
            st[('namelen128', len(A['name'])==128)]+=1
            if 'ISOA' in [k.decode() for k in A['blocks']]: st[('isoa ok', len(A['blocks'][b'ISOA'])==24*A['nf'])]+=1
            ex['abc'].append(A['abc'])
            fr=collections.Counter(x[2:].hex() for x in A['frames']); 
            for k in fr: st[('frame tail',k)]+=1
            st[('img idx in range', all(struct.unpack('<H',x[:2])[0]<A['ni'] for x in A['frames']))]+=1
    for k,v in sorted(st.items(),key=str): print(k,v)
    print(ex['fail'][:5]); print(collections.Counter(ex['abc']).most_common(20))
