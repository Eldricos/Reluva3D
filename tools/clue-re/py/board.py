# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import re,collections,sys
rows=[]
for line in open(sys.argv[1]):
    m=re.match(r'(\S+)\s+layer (\d+)\s+at\s+(-?\d+),\s*(-?\d+)\s+id\s+(-?\d+)',line)
    if m: rows.append((m.group(1),int(m.group(2)),int(m.group(3)),int(m.group(4))))
cell=lambda x,y:((x-16)//64,(y-16)//64)
hall=set(); floor={}
for n,l,x,y in rows:
    d=n.split(':')[0]
    if d=='r_hwfloor' and l==0: hall.add(cell(x,y))
    elif d.startswith('r_'): floor[cell(x,y)]=d
# rooms: connected components of non-hallway floor cells
comp={}; comps=[]
for c in floor:
    if c in comp: continue
    k=len(comps); comps.append([]); st=[c]; comp[c]=k
    while st:
        a=st.pop(); comps[k].append(a)
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
            b=(a[0]+dx,a[1]+dy)
            if b in floor and b not in comp: comp[b]=k; st.append(b)
# label by furniture drives inside
names={'ballroom':'Ballroom','billiardroom':'Billiard Room','conservatory':'Conservatory','diningroom':'Dining Room','hallroom':'Hall','kitchen':'Kitchen','library':'Library','lounge':'Lounge','study':'Study'}
votes=collections.defaultdict(collections.Counter)
for n,l,x,y in rows:
    d=n.split(':')[0]
    if d in names and l==5:
        c=cell(x,y)
        if c in comp: votes[comp[c]][names[d]]+=1
label={k:(votes[k].most_common(1)[0][0] if votes[k] else 'centre') for k in range(len(comps))}
# doors from the door tops
doors=[]
for n,l,x,y in rows:
    if 'drtop' not in n: continue
    bx,by=(x-16)/64,(y-16)/64
    cands=[]
    if bx==int(bx): cands.append(((int(bx)-1,int(by)),(int(bx),int(by))))
    if by==int(by): cands.append(((int(bx),int(by)-1),(int(bx),int(by))))
    found=None
    for a,b in cands:
        for r,h in ((a,b),(b,a)):
            if r in comp and h in hall: found=(label[comp[r]],r,h)
    doors.append((n,found))
chars=[(n.split(':O')[1],cell(x,y),(x-16)%64,(y-16)%64) for n,l,x,y in rows if n.startswith('character:')]
sps=[(n,label.get(comp.get(cell(x,y)),'?')) for n,l,x,y in rows if n.startswith('secretpass:')]
if __name__=='__main__':
    print('hallway squares',len(hall))
    for k,c in enumerate(comps): print(f"{label[k]:14s} {len(c):3d} squares, x {min(a for a,b in c)}-{max(a for a,b in c)}, y {min(b for a,b in c)}-{max(b for a,b in c)}")
    for d in doors: print('door',d)
    print('starts',chars); print('passages',sps)
