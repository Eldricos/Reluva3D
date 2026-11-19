# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import struct, sys, glob, collections, zlib, os
def load(p):
    d=open(p,'rb').read()
    if d[:4]==b'PACK':
        c,u=struct.unpack('<II',d[4:12]); d=zlib.decompress(d[12:12+c]); assert len(d)==u
    return d
def entries(d):
    assert d[:4]==b'DDRV'
    dirofs,cnt=struct.unpack('<II',d[4:12])
    tail=struct.unpack('<I',d[-4:])[0]
    assert dirofs+cnt*148+4==len(d), (dirofs,cnt,len(d))
    assert tail==cnt
    out=[]
    for i in range(cnt):
        e=d[dirofs+i*148:dirofs+(i+1)*148]
        flags,ft,off,size=struct.unpack('<IQII',e[:20]); name=e[20:].split(b'\0')[0].decode('latin-1')
        out.append((flags,ft,off,size,name,e[20:]))
    return out
if __name__=='__main__':
    files=[f for f in glob.glob('clue_dl/**/*',recursive=True) if os.path.isfile(f) and f.lower().endswith(('.res','.a','.o','.lvl'))]
    st=collections.Counter(); bad=[]
    for f in files:
        d=load(f)
        if d[:4]!=b'DDRV': st['not ddrv '+d[:4].decode('latin-1')]+=1; continue
        try: es=entries(d)
        except AssertionError as e: bad.append((f,e)); continue
        st['ok']+=1
        pos=12
        for fl,ft,off,size,name,raw in es:
            st[('flags',fl)]+=1
            if off!=pos: st['gap']+=1
            pos=off+size
            tag=d[off:off+4]; st[('tag',tag)]+=1
            if any(raw[len(name)+1:]): st['junk after name']+=1
        if pos!=struct.unpack('<I',d[4:8])[0]: st['end mismatch']+=1
    print(len(files),st); print(bad[:5])
