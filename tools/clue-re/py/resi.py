import struct,sys
def parse(d):
    assert d[:4]==b'RESI'; nd,nf=d[4],d[5]; p=6
    def s():
        nonlocal p; n=d[p]; p+=1
        if n==0: return ''
        t=d[p:p+n].decode('latin-1'); p+=n+1; return t
    fonts=[]
    for i in range(nf):
        fid=struct.unpack('<I',d[p:p+4])[0]; p+=4; name=s(); ts,ls=struct.unpack('<ii',d[p:p+8]); p+=8
        fonts.append((fid,name,ts,ls))
    dialogs=[]
    for i in range(nd):
        hdr=d[p:p+36]; p+=36; name=s(); nc=d[p]; p+=1; ctrls=[]
        for c in range(nc):
            blk=d[p:p+112]; p+=112; text=s(); anim=s(); ni=struct.unpack('<H',d[p:p+2])[0]; p+=2
            items=[s() for k in range(ni)]
            ctrls.append((blk,text,anim,items))
        dialogs.append((hdr,name,ctrls))
    return fonts,dialogs,p
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); fonts,dialogs,p=parse(d)
    print('fonts',fonts); print('parsed to',p,'of',len(d))
    for hdr,name,ctrls in dialogs:
        h=struct.unpack('<9i',hdr)
        print('DIALOG',repr(name),'hdr',[x if abs(x)<100000 else 'P' for x in h],len(ctrls),'controls')
        for blk,text,anim,items in ctrls:
            v=struct.unpack('<28i',blk)
            print('   t%d id%-3d x%-4d y%-4d w%-4d h%-4d'%v[:6], [x if abs(x)<100000 else 'P' for x in v[8:28]], repr(text)[:40], anim, items[:4])
