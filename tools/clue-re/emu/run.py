# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import struct, re, sys
from unicorn import *
from unicorn.x86_const import *
EXE=os.path.join(GAME, 'Clue.exe')
d=open(EXE,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<8sIIII',d,pe+24+opt+40*i) for i in range(n)]
def off(rva):
    for nm,vs,va,rs,ra in secs:
        if va<=rva<va+max(vs,rs): return rva-va+ra
mu=Uc(UC_ARCH_X86,UC_MODE_32)
mu.mem_map(0x400000,0x80000)
for nm,vs,va,rs,ra in secs: mu.mem_write(0x400000+va,d[ra:ra+min(rs,max(vs,rs))])
STUB=0x20000000; mu.mem_map(STUB,0x100000)
HEAP=0x30000000; mu.mem_map(HEAP,0x1000000); heap=[HEAP+0x1000]
STACK=0x7f000000; mu.mem_map(STACK-0x100000,0x100000)
DATA=0x40000000; mu.mem_map(DATA,0x100000); data=[DATA]
def alloc(sz,base=heap):
    a=(base[0]+15)&~15; base[0]=a+sz; mu.mem_write(a,b'\0'*sz); return a
stubs={}  # addr -> name
def stub(name):
    a=STUB+16*len(stubs); stubs[a]=name; mu.mem_write(a,b'\xc3'); return a
# imports
imp=struct.unpack_from('<I',d,pe+24+104)[0]; o=off(imp)
iat={}
while True:
    ilt,ts,fc,nm,iatr=struct.unpack_from('<5I',d,o); o+=20
    if nm==0: break
    dll=d[off(nm):d.index(b'\0',off(nm))].decode()
    k=0
    while True:
        e=struct.unpack_from('<I',d,off(ilt or iatr)+4*k)[0]
        if e==0: break
        name=('%s#%d'%(dll,e&0xffff)) if e&0x80000000 else d[off(e)+2:d.index(b'\0',off(e)+2)].decode()
        iat[0x400000+iatr+4*k]=name; k+=1
def mgr_object(prefix,slots=64):
    obj=alloc(64,data); vt=alloc(4*slots,data)
    for s in range(slots): mu.mem_write(vt+4*s,struct.pack('<I',stub('%s::virt%d'%(prefix,s*4))))
    mu.mem_write(obj,struct.pack('<I',vt)); return obj
for slot,name in iat.items():
    if re.match(r'\?.*@@[0-2][A-Z]',name) and not name.endswith('Z'):  # data import
        cell=alloc(64,data)
        if 'm_instance' in name: mu.mem_write(cell,struct.pack('<I',mgr_object('WinMgr')))
        elif 'EagleManager' in name: mu.mem_write(cell,struct.pack('<I',mgr_object('EagleMgr')))
        elif 'CRuntimeClass' in name: mu.mem_write(cell+12,struct.pack('<I',stub('CreateFake:'+name)))
        mu.mem_write(slot,struct.pack('<I',cell))
    else:
        mu.mem_write(slot,struct.pack('<I',stub(name)))
# arg sizes from MSVC mangling
def argbytes(name):
    m=re.match(r'\?[^@]+@(?:[^@]+@)*@([A-Z])([A-Z]?)',name)
    if not m: return None
    body=name[name.index('@@')+2:]
    if body[0] in 'SKC' :   # static: callconv next
        cc=body[1]; rest=body[2:]
    elif body[0]=='Y': cc=body[1]; rest=body[2:]
    else:
        cc=body[2]; rest=body[3:]
    i=0
    def typ(i):
        c=rest[i]
        if c in 'PQAB':
            i+=2  # cv
            return typ(i)[0]+0 and None or (4, skip(i))
        if c in 'VUW':
            j=rest.index('@@',i); return None,j+2
        if c=='_': return 8 if rest[i+1] in 'JK' else 4, i+2
        if c.isdigit(): return 4,i+1
        if c in 'NO': return 8,i+1
        return 4,i+1
    def qname(j):
        while True:
            if rest[j]=='@': return j+1
            if rest[j].isdigit(): j+=1
            else: j=rest.index('@',j)+1
    def skip(i):
        c=rest[i]
        if c in 'PQAB': return skip(i+2)
        if c in 'VU': return qname(i+1)
        if c=='W': return qname(i+2)
        if c=='_': return i+2
        return i+1
    def t2(i):
        c=rest[i]
        if c in 'PQAB': return 4,skip(i)
        if c in 'VUW': return 4,skip(i)
        if c=='_': return (8 if rest[i+1] in 'JK' else 4),i+2
        if c in 'NO': return 8,i+1
        return 4,i+1
    _,i=t2(0)  # return type
    tot=0
    if rest[i]=='X': return cc,0
    while i<len(rest) and rest[i] not in '@Z':
        s,i=t2(i); tot+=s
    return cc,tot
import pickle
INS=pickle.load(open(os.path.join(WORK, 'insns.pkl'),'rb'))
INDEX={a:k for k,(a,m,o) in enumerate(INS)}
def pushes_before(ra):
    k=INDEX.get(ra)
    if k is None: return 0
    k-=1; n=0; j=k-1
    while j>k-20:
        a,m,o=INS[j]
        if m=='call' or (m=='add' and o.startswith('esp')) or m in ('ret','jmp') : break
        if m=='push': n+=1
        j-=1
    return n
windows={}   # obj -> dict
byid={}
images={}    # handle->name
sizes={}
for l in open(os.path.join(WORK, 'sizes.txt')):
    p=l.split(); sizes[p[0].lower()]=(int(p[1]),int(p[2]))
def rd(a): return struct.unpack('<I',mu.mem_read(a,4))[0]
def rds(a):
    b=bytes(mu.mem_read(a,128)); return b[:b.index(0)].decode('latin1')
def arg(i): return rd(mu.reg_read(UC_X86_REG_ESP)+4+4*i)
def win(obj):
    if obj not in windows: windows[obj]={'id':None,'x':0,'y':0,'w':0,'h':0,'parent':0,'img':None,'shown':None}
    return windows[obj]
def absrect(obj):
    w=windows.get(obj); x=y=0
    if not w: return (0,0,0,0)
    o=obj; ax=ay=0; depth=0
    while o and o in windows and depth<10:
        ax+=windows[o]['x']; ay+=windows[o]['y']; o=windows[o]['parent']; depth+=1
    return ax,ay,ax+w['w'],ay+w['h']
DEBUG=False
DLG={30:(201,409),31:(201,157),32:(111,222),33:(237,408),34:(640,480),44:(231,395)}
imgcalls=set()
unknown=set(); calls=[]
CSTR={540:('ctor0',0),535:('ctor1',4),537:('ctor1',4),800:('dtor',0),858:('assign',4),860:('assign',4),
      939:('append',4),941:('append',4),940:('appendc',4),922:('concat',12),923:('concat',12),924:('concat',12),
      2614:('empty',0),2915:('ptr',4)}
bufs={}
def setstr(obj,t):
    b=alloc(len(t)+32); mu.mem_write(b+12,t.encode('latin1')+b'\0'); bufs[b+12]=t; mu.mem_write(obj,struct.pack('<I',b+12))
def text(x):
    if x in bufs: return bufs[x]
    try:
        p=rd(x)
        if p in bufs: return bufs[p]
    except Exception: pass
    try: return rds(x)
    except Exception: return ''
nexthandle=[0x5000]
def handle(name):
    ecx=mu.reg_read(UC_X86_REG_ECX); ret=0; nm=name
    if name=='SHW32.dll#70':   # SmartHeap MemAllocPtr(pool, size, flags), stdcall
        a=alloc(max(arg(1),16)); esp=mu.reg_read(UC_X86_REG_ESP); ra=rd(esp)
        mu.reg_write(UC_X86_REG_EAX,a); mu.reg_write(UC_X86_REG_ESP,esp+16); mu.reg_write(UC_X86_REG_EIP,ra); return
    if name.startswith('SHW32.dll#'):
        calls.append(name)
        esp=mu.reg_read(UC_X86_REG_ESP); ra=rd(esp)
        mu.reg_write(UC_X86_REG_EAX,1); mu.reg_write(UC_X86_REG_ESP,esp+4); mu.reg_write(UC_X86_REG_EIP,ra); return
    if name.startswith('MFC42.DLL#') and int(name.split('#')[1]) in CSTR:
        o=int(name.split('#')[1]); kind,pop=CSTR[o]
        if kind=='ctor0': setstr(ecx,''); ret=ecx
        elif kind=='ctor1': setstr(ecx,text(arg(0))); ret=ecx
        elif kind=='dtor': pass
        elif kind=='assign': setstr(ecx,text(arg(0))); ret=ecx
        elif kind=='append': setstr(ecx,text(ecx)+text(arg(0))); ret=ecx
        elif kind=='appendc': setstr(ecx,text(ecx)+chr(arg(0)&0xff)); ret=ecx
        elif kind=='concat': setstr(arg(0),text(arg(1))+text(arg(2))); ret=arg(0)
        elif kind=='empty': setstr(ecx,'')
        elif kind=='ptr': ret=rd(ecx)
        esp=mu.reg_read(UC_X86_REG_ESP); ra=rd(esp)
        mu.reg_write(UC_X86_REG_EAX,ret); mu.reg_write(UC_X86_REG_ESP,esp+4+pop); mu.reg_write(UC_X86_REG_EIP,ra); return
    if name.startswith('CreateFake:'):
        obj=mgr_object('Fake'); esp=mu.reg_read(UC_X86_REG_ESP); ra=rd(esp)
        mu.reg_write(UC_X86_REG_EAX,obj); mu.reg_write(UC_X86_REG_ESP,esp+4); mu.reg_write(UC_X86_REG_EIP,ra); return
    if name=='MFC42.DLL#2233':   # CRuntimeClass::CreateObject: on to its m_pfnCreateObject
        mu.reg_write(UC_X86_REG_EIP, rd(ecx+12)); return
    short=re.sub(r'@@.*','',name)
    if short=='?Create@EGuiWindow' and name.startswith('?Create@EGuiWindow@@IAE'):
        style,ex,x,y,w,h,parent,id_=[arg(i) for i in range(8)]
        if DEBUG: print('Create from',hex(rd(mu.reg_read(UC_X86_REG_ESP))),[hex(arg(i)) for i in range(8)])
        W=win(ecx); W.update(id=id_,x=x,y=y,w=w,h=h,parent=parent); byid[id_]=ecx; ret=1
    elif short=='?Create@EGuiWindow':  # static
        rc,style,ex,rect,parent,id_=[arg(i) for i in range(6)]
        obj=mgr_object('Static%x'%id_); l,t,r,b=struct.unpack('<4i',mu.mem_read(rect,16))
        W=win(obj); W.update(id=id_,x=l,y=t,w=r-l,h=b-t,parent=parent); byid[id_]=obj; ret=obj
    elif short=='?GetDlgItem@EGuiWindowManager':
        parent,id_=arg(0),arg(1); ret=byid.get(id_,0)
        if not ret and id_ in DLG:
            obj=mgr_object('Dlg%d'%id_); W=win(obj); W.update(id=id_,w=DLG[id_][0],h=DLG[id_][1],parent=0,img='dialog %d'%id_); byid[id_]=obj; ret=obj
        if not ret: print('GetDlgItem missing',hex(id_))
    elif short=='?GetMasterWindow@EGuiWindowManager':
        ret=byid.get(0x2712,0)
    elif short=='?GetWindowRect@EGuiWindow':
        r=absrect(ecx); mu.mem_write(arg(0),struct.pack('<4i',*r))
    elif short=='?GetClientRect@EGuiWindow':
        W=win(ecx); mu.mem_write(arg(0),struct.pack('<4i',0,0,W['w'],W['h']))
    elif short=='?MoveWindow@EGuiWindowManager':
        w0=arg(0); x,y,w,h=[struct.unpack('<i',struct.pack('<I',arg(i)))[0] for i in range(1,5)]
        W=win(w0); W.update(x=x,y=y,w=w,h=h); W.setdefault('moves',[]).append((x,y,w,h))
    elif short=='?MoveWindow@EGuiWindow' and 'PBUtagRECT' in name:
        l,t,r,b=struct.unpack('<4i',mu.mem_read(arg(0),16))
        W=win(ecx); W.update(x=l,y=t,w=r-l,h=b-t); W.setdefault('moves',[]).append((l,t,r-l,b-t))
    elif short=='?MoveWindow@EGuiWindow':
        x,y,w,h=[struct.unpack('<i',struct.pack('<I',arg(i)))[0] for i in range(4)]
        W=win(ecx); W.update(x=x,y=y,w=w,h=h); W.setdefault('moves',[]).append((x,y,w,h))
        # MoveWindow coordinates are relative to parent
    elif short=='?ShowWindow@EGuiWindowManager':
        win(arg(0))['shown']=arg(1); ret=1
    elif name=='SetRect':
        mu.mem_write(arg(0),struct.pack('<4i',*[struct.unpack('<i',struct.pack('<I',arg(i)))[0] for i in range(1,5)])); ret=1
    elif short in ('?LoadBackgroundImage@EGuiWindow','?LoadBackgroundAnimation@EGuiWindow'):
        st=arg(0); nmx=rds(arg(1)); W=win(ecx)
        W.setdefault('imgs',{})[st]=nmx
        if W['img'] is None or st==4: W['img']=nmx
        ret=1
    elif short=='?SizeToContent@EGuiWindow' or re.match(r'(Fake|Static[0-9a-f]+)::virt32$',name):
        W=win(ecx); s2=sizes.get((W['img'] or '').lower())
        if s2: W['w'],W['h']=s2
        else: W['sizeless']=True
    elif name=='SetRectEmpty':
        mu.mem_write(arg(0),b'\0'*16); ret=1
    elif name=='OffsetRect':
        r=list(struct.unpack('<4i',mu.mem_read(arg(0),16))); dx,dy=[struct.unpack('<i',struct.pack('<I',arg(i)))[0] for i in (1,2)]
        r=[r[0]+dx,r[1]+dy,r[2]+dx,r[3]+dy]; mu.mem_write(arg(0),struct.pack('<4i',*r)); ret=1
    elif name.startswith('EagleMgr::virt') and re.match(r'^[A-Za-z_0-9]+:[A-Za-z]', (lambda: (lambda t: t)(rds(arg(0))) if True else '')() or ''):
        nmx=rds(arg(0)); h=nexthandle[0]; nexthandle[0]+=1; images[h]=nmx; ret=h
    elif name in ('??2@YAPAXI@Z','malloc','operator new') or name.endswith('#823') or name.endswith('??2@YAPAXI@Z'):
        ret=alloc(max(arg(0),16))
    else:
        # anything taking an image handle: remember it on the window
        for i in range(4):
            try: v=arg(i)
            except: break
            if v in images:
                W=win(ecx); W['img']=images[v]; W.setdefault('imgs',{})['h%d'%i]=images[v]; imgcalls.add(name)
        unknown.add(name); calls.append(name)
        if name.startswith('??0'): ret=ecx
    # return
    fixed={}
    if name in fixed: ab=fixed[name]
    else:
     try: ab=argbytes(name)
     except Exception: print('demangle?',name); ab=None
    if name.startswith(('EagleMgr::virt','WinMgr::virt')) or name.startswith(('Static','Fake')):
        cc,n2=('E',4*pushes_before(rd(mu.reg_read(UC_X86_REG_ESP))))
    elif name in ('SetRect',): cc,n2='G',20
    elif name=='SetRectEmpty': cc,n2='G',4
    elif name=='OffsetRect': cc,n2='G',12
    elif ab: cc,n2=ab
    else: cc,n2='A',0
    pop = n2 if cc in 'EG' else 0
    esp=mu.reg_read(UC_X86_REG_ESP); ra=rd(esp)
    mu.reg_write(UC_X86_REG_EAX,ret); mu.reg_write(UC_X86_REG_ESP,esp+4+pop); mu.reg_write(UC_X86_REG_EIP,ra)
def hook(uc,addr,size,ud):
    if addr in stubs: handle(stubs[addr])
mu.hook_add(UC_HOOK_CODE,hook,begin=STUB,end=STUB+0x100000)
def bad(uc,access,addr,size,value,ud):
    print('bad access',hex(addr),'at',hex(uc.reg_read(UC_X86_REG_EIP))); return False
mu.hook_add(UC_HOOK_MEM_UNMAPPED,bad)
trail=[]
def blk(uc,addr,size,ud):
    trail.append(addr)
    if len(trail)>30: trail.pop(0)
mu.hook_add(UC_HOOK_BLOCK,blk)
# the preferences' getter: a zeroed block
PREFS=alloc(0x1000,data)
mu.mem_write(0x42fc60, b'\xb8'+struct.pack('<I',PREFS)+b'\xc3')
# run 0x435050(this, 0x460428)
this=alloc(0x400)
tib=alloc(0x1000,data); mu.mem_write(tib,struct.pack('<I',0xffffffff)); mu.mem_write(tib+0x18,struct.pack('<I',tib))
GDT=alloc(0x100,data)
def desc(base,limit,access,flags):
    return struct.pack('<Q', (limit&0xffff) | ((base&0xffffff)<<16) | (access<<40) | (((limit>>16)&0xf)<<48) | (flags<<52) | (((base>>24)&0xff)<<56))
mu.mem_write(GDT+8*3, desc(tib,0xfff,0x93,0x4))
mu.reg_write(UC_X86_REG_GDTR,(0,GDT,0xff,0))
mu.mem_write(GDT+8*1, desc(0,0xfffff,0x9b,0xc))
mu.mem_write(GDT+8*2, desc(0,0xfffff,0x93,0xc))
mu.reg_write(UC_X86_REG_FS,3<<3)
for r in (UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_SS,UC_X86_REG_GS): mu.reg_write(r,2<<3)
SENT=STUB+0xff000; mu.mem_write(SENT,b'\xf4')
esp=STACK-0x1000
esp-=4; mu.mem_write(esp,struct.pack('<I',0x460428))
esp-=4; mu.mem_write(esp,struct.pack('<I',SENT))
mu.reg_write(UC_X86_REG_ESP,esp); mu.reg_write(UC_X86_REG_ECX,this)
try:
    mu.emu_start(int(sys.argv[1],16) if len(sys.argv)>1 else 0x435050,SENT,count=2000000)
except UcError as e:
    print('stopped',e,hex(mu.reg_read(UC_X86_REG_EIP))); print([hex(t) for t in trail]); print('lastcalls',calls[-5:], [stubs.get(t) for t in trail if t in stubs][-4:], 'ecx',hex(mu.reg_read(UC_X86_REG_ECX)))
print('imgcalls',imgcalls); print('unknown imports:',sorted(unknown)); print('calls',calls[:200])
for obj,W in sorted(windows.items(),key=lambda kv:(kv[1]['id'] or 0)):
    print(hex(W['id'] or 0), 'abs',absrect(obj), 'parent', hex(windows[W['parent']]['id']) if W['parent'] in windows and windows[W['parent']]['id'] else hex(W['parent']), W['img'], 'moves',W.get('moves'))

open(os.path.join(WORK, 'names.txt'),'w').write('\n'.join(sorted(set(n for W in windows.values() for n in list(W.get('imgs',{}).values())+[W['img']] if n))))
