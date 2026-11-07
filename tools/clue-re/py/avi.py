import sys,struct,collections
def walk(d,p,end,depth,out):
    while p+8<=end:
        cid=d[p:p+4]; sz=struct.unpack('<I',d[p+4:p+8])[0]
        if cid in (b'RIFF',b'LIST'):
            typ=d[p+8:p+12]; out.append((depth,cid+b' '+typ,sz,p)); walk(d,p+12,p+8+sz,depth+1,out)
        else: out.append((depth,cid,sz,p))
        p+=8+sz+(sz&1)
for f in sys.argv[1:]:
    d=open(f,'rb').read(); out=[]; walk(d,0,len(d),0,out)
    c=collections.Counter((dep,cid) for dep,cid,sz,p in out)
    print('==',f.split('/')[-1],len(d)); 
    for (dep,cid),n in sorted(c.items(),key=lambda x:x[0]): print('  '*dep,cid,n)
    for dep,cid,sz,p in out:
        if cid in (b'avih',b'strh',b'strf'): print('   ',cid,d[p+8:p+8+min(sz,56)].hex(' '))
