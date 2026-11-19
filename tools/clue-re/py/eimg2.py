# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import sys,struct,collections,glob
sys.path.insert(0,'tools'); from ddrv import load, entries
def header(r):
    ver=struct.unpack('<I',r[4:8])[0]
    if ver==0x200:
        typ,w,h=struct.unpack('<III',r[12:24]); b24=r[24]; pitch=struct.unpack('<I',r[25:29])[0]
        mid=r[29:41]; w2,h2=struct.unpack('<II',r[41:49]); key=r[49:53]; ds=struct.unpack('<I',r[53:57])[0]; data=r[57:57+ds]
        return dict(ver=ver,typ=typ,w=w,h=h,b24=b24,pitch=pitch,mid=mid,w2=w2,h2=h2,key=key,ds=ds,data=data,hdr=57)
    else:
        w,h,bpp,typ=struct.unpack('<HHII',r[12:24]); mid=r[24:40]; w2,h2=struct.unpack('<HH',r[40:44]); key=r[44:48]; ds=struct.unpack('<I',r[48:52])[0]
        return dict(ver=ver,typ=typ,w=w,h=h,bpp=bpp,mid=mid,w2=w2,h2=h2,key=key,ds=ds,data=r[52:52+ds],hdr=52)
def rle(data,w,h):
    i=0; rows=0; px=0; over=0
    for y in range(h):
        x=0
        while i<len(data):
            c=data[i]; i+=1
            if c==0x80: break
            if c&0x80: x+=c&0x7f; continue
            i+=2*c; x+=c; px+=c
        if x>w: over+=1
        rows+=1
        if i>=len(data): break
    return i,rows,over
if __name__=='__main__':
    st=collections.Counter(); ex=collections.defaultdict(list)
    for f in glob.glob('clue_dl/ClueInstall/Data/Classic/**/*.res',recursive=True)+glob.glob('clue_dl/ClueMABM/clue22/*.res'):
        d=load(f)
        if d[:4]!=b'DDRV': continue
        for fl,ft,off,size,name,raw in entries(d):
            r=d[off:off+size]; H=header(r)
            st[('ver',H['ver'],'typ',H['typ'])]+=1
            if H['hdr']+H['ds']!=len(r): st['size mismatch']+=1
            if (H['w'],H['h'])!=(H['w2'],H['h2']): st[('sizes differ',H['ver'])]+=1; ex['diff'].append((f.split('/')[-1],name,H['w'],H['h'],H['w2'],H['h2']))
            if any(H['mid']): st[('mid nonzero',H['ver'])]+=1; ex['mid'].append((f.split('/')[-1],name,H['mid'].hex()))
            st[('key',H['key'].hex())]+=1
            if H['ver']==0x200: st[('b24',H['b24'], 'pitch ok', H['pitch']==((H['w']+1)//2*2)*2)]+=1
            else: st[('bpp',H['bpp'])]+=1
            used,rows,over=rle(H['data'],H['w'],H['h'])
            st['exact' if used==len(H['data']) and rows==H['h'] else ('short' if rows<H['h'] else 'leftover')]+=1
            if over: st['row overflow']+=1
            if not(used==len(H['data']) and rows==H['h']): ex['inexact'].append((f.split('/')[-1],name,H['ver'],H['w'],H['h'],len(H['data']),used,rows))
    for k,v in sorted(st.items(),key=str): print(k,v)
    for k,v in ex.items(): print(k,len(v),v[:12])
