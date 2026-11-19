# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import sys,struct,collections; sys.path.insert(0,'tools')
from ddrv import load, entries
def level(path):
    d=load(path); E={e[4]:d[e[2]:e[2]+e[3]] for e in entries(d)}
    w=E['/World.dat']; title=w[:w.index(b'\0')].decode()
    p=40; n=struct.unpack('<H',w[p:p+2])[0]; p+=2; names=[]
    for i in range(n): L=w[p]; names.append(w[p+1:p+1+L].decode()); p+=1+L
    o=E['/Objects.dat']; cnt=struct.unpack('<H',o[:2])[0]; q=2; recs=[]
    while q<len(o):
        L=struct.unpack('<H',o[q:q+2])[0]; recs.append(o[q+2:q+L]); q+=L
    return dict(title=title,names=names,wrest=w[p:],count=cnt,recs=recs,end=q,olen=len(o),E=E)
if __name__=='__main__':
    L=level(sys.argv[1])
    print(L['title'],len(L['names']),'names; world rest',len(L['wrest']),L['wrest'][:40].hex(' '))
    print('objects count',L['count'],'records',len(L['recs']),'end',L['end'],L['olen'])
    st=collections.Counter((r[0],len(r)) for r in L['recs']); print(sorted(st.items()))
