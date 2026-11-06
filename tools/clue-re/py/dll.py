import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys,pefile,capstone
d=pefile.PE(os.path.join(GAME, 'ImageRLE16.dll')); base=d.OPTIONAL_HEADER.ImageBase
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.skipdata=True
a=int(sys.argv[1],16); n=int(sys.argv[2])
for k,i in enumerate(md.disasm(d.get_data(a-base,n*8),a)):
    if k>=n: break
    print('   %08x %s %s'%(i.address,i.mnemonic,i.op_str))
