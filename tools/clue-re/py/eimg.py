import sys,struct,re
from PIL import Image
def recs(d):
    tot,cnt=struct.unpack('<II',d[4:12]); p=12
    for i in range(cnt):
        tag=d[p:p+4]; ver,size=struct.unpack('<II',d[p+4:p+12])
        yield tag,d[p:p+12+size]; p+=12+size
def decode(r):
    w,h=struct.unpack('<II',r[16:24]); ds=struct.unpack('<I',r[53:57])[0]
    data=r[57:57+ds]; img=Image.new('RGBA',(w,h)); px=img.load(); i=0
    for y in range(h):
        x=0
        while i<len(data):
            c=data[i]; i+=1
            if c==0x80: break
            if c&0x80: x+=c&0x7f; continue
            for k in range(c):
                v=data[i]|data[i+1]<<8; i+=2
                if x<w: px[x,y]=(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255)
                x+=1
    return img,i,len(data)
fn,out,maxn=sys.argv[1],sys.argv[2],int(sys.argv[3])
d=open(fn,'rb').read(); n=0
for tag,r in recs(d):
    if tag!=b'EIMG': continue
    img,used,ds=decode(r)
    if n<3: print(fn,n,img.size,"used",used,"of",ds)
    img.save(f"{out}_{n:03d}.png"); n+=1
    if n>=maxn: break
