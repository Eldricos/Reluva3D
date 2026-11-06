import sys, pefile, capstone
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.skipdata=True
insns=list(md.disasm(text.get_data(),base+text.VirtualAddress))
idx={i.address:k for k,i in enumerate(insns)}
if sys.argv[2]=='dis':
    a=int(sys.argv[3],16); k=idx[a]
    for j in insns[k:k+int(sys.argv[4])]: print(f"   {j.address:08x} {j.mnemonic} {j.op_str}")
    sys.exit()
tgt=[int(x,16) for x in sys.argv[2].split(',')]; n=int(sys.argv[3])
for k,ins in enumerate(insns):
    if ins.mnemonic=='call' and ins.op_str.startswith('0x') and int(ins.op_str,16) in tgt:
        print(f"--- {hex(ins.address)} -> {ins.op_str}")
        for j in insns[max(0,k-n):k+1]: print(f"   {j.address:08x} {j.mnemonic} {j.op_str}")
