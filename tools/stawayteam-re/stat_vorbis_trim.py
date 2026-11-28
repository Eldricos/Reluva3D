#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Compare how two Vorbis decoders trim the ends of the game's sounds.

Star Trek: Away Team plays its Ogg Vorbis files through the vorbis.dll it
ships (libVorbis I 20010225), whose vorbis_synthesis_blockin (at
0x10001ed0 in that DLL) cuts a stream's partial last frame when the packet
ending the stream has a granule position short of the samples decoded
(0x1000223d), and cuts the start only at the second audio packet
(0x10002250). libvorbis 1.3.7, which Reluva decodes with, cuts the start
where the first granule position is short. This works out the samples
each would give from the files' Ogg pages and the packets' block sizes,
without decoding, and prints both for each file named:

    python3 stat_vorbis_trim.py extracted/Sounds/*.ogg

Its 1.3.7 count was checked against libvorbis 1.3.7 decoding all 1,484 of
the game's sounds, the same for every one.
"""

import struct
import sys
def pages(d):
    p=0
    while p+27<=len(d) and d[p:p+4]==b'OggS':
        flags=d[p+5]; gp=struct.unpack_from('<q',d,p+6)[0]; nseg=d[p+26]
        segs=d[p+27:p+27+nseg]; q=p+27+nseg
        yield flags,gp,segs,d[q:q+sum(segs)]
        p=q+sum(segs)
def packets(d):
    """Each packet with the granule position libogg gives it (-1 but for
    the last ending on a page) and whether it ends the stream."""
    out=[]; buf=b''
    for flags,gp,segs,body in pages(d):
        o=0; ends=[]
        for s in segs:
            buf+=body[o:o+s]; o+=s
            if s<255: ends.append(buf); buf=b''
        for i,pk in enumerate(ends):
            last=i==len(ends)-1
            out.append([pk, gp if last else -1, bool(flags&4) and last])
    return out
class BR:
    def __init__(s,b): s.b=b; s.p=0
    def bits(s,n):
        v=0
        for i in range(n):
            byte=s.b[s.p>>3]; v|=((byte>>(s.p&7))&1)<<i; s.p+=1
        return v
def modes_from_setup(setup):
    """The modes' block flags, read back from the setup header's end."""
    b=setup; tot=len(b)*8
    def bit(i): return (b[i>>3]>>(i&7))&1
    # find framing bit: last set bit
    i=tot-1
    while i>=0 and not bit(i): i-=1
    # i is framing bit; walk back over modes (each 41 bits: 1 blockflag,16 wt,16 tt,8 map)
    pos=i  # start of framing
    modes=[]
    cand=[]
    while pos-41>=0:
        start=pos-41
        r=BR(b); r.p=start
        bf=r.bits(1); wt=r.bits(16); tt=r.bits(16); mp=r.bits(8)
        if wt!=0 or tt!=0: break
        modes.insert(0,bf); pos=start
        # check count field
        r2=BR(b); r2.p=pos-6
        if pos>=6 and r2.bits(6)+1==len(modes):
            cand=list(modes)
    return cand
def sim(path):
    d=open(path,'rb').read()
    pk=packets(d)
    ident=pk[0][0]; bs0=1<<(ident[28]&15); bs1=1<<(ident[28]>>4); ch=ident[11]
    modes=modes_from_setup(pk[2][0])
    mbits=(len(modes)-1).bit_length()
    audio=pk[3:]
    bss=[]
    for p,g,e in audio:
        if not p or p[0]&1: bss.append(None); continue
        r=BR(p); r.bits(1); m=r.bits(mbits) if mbits else 0
        bss.append(bs1 if modes[m] else bs0)
    # produced per packet
    prod=[0]+[bss[i-1]//4+bss[i]//4 for i in range(1,len(bss))]
    # libVorbis 20010225's rules
    vgp=-1; cur=0; ret=0
    for seq,((p,g,e),n) in enumerate(zip(audio,prod)):
        cur+=n
        if vgp==-1:
            vgp = 0 if g==-1 else g
            continue
        vgp+=n
        if g!=-1 and vgp!=g:
            if vgp>g:
                extra=vgp-g
                if e: cur-=extra
                elif seq==1:
                    ret=min(ret+extra,cur)
            vgp=g
    old=cur-ret
    # libvorbis 1.3.7's
    vgp=-1; cur=0; ret=0; cnt=0
    for seq,((p,g,e),n) in enumerate(zip(audio,prod)):
        cur+=n; cnt+=n
        if vgp==-1:
            if g!=-1:
                vgp=g
                if cnt>vgp:
                    extra=max(0,cnt-g)
                    if e: cur-=extra
                    else: ret=min(ret+extra,cur)
        else:
            vgp+=n
            if g!=-1 and vgp!=g:
                if vgp>g and e: cur-=vgp-g
                vgp=g
    new=cur-ret
    return old,new


def main():
    for f in sys.argv[1:]:
        try:
            old, new = sim(f)
            print(old, new, f, sep='\t')
        except (OSError, IndexError, ValueError) as ex:
            print('ERR', ex, f, sep='\t')


if __name__ == '__main__':
    main()
