import sys, pefile, capstone
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
iat={}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports: iat[i.address]=(e.dll.decode()+'!'+(i.name.decode() if i.name else f'#{i.ordinal}'))
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
code=text.get_data(); va=base+text.VirtualAddress
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.skipdata=True
want=sys.argv[2]
insns=list(md.disasm(code,va))
for k,ins in enumerate(insns):
    if ins.mnemonic in('call','jmp','mov') and '[0x' in ins.op_str:
        try: a=int(ins.op_str.split('[0x')[1].split(']')[0],16)
        except: continue
        if a in iat and want in iat[a]:
            print(f"--- {hex(ins.address)} -> {iat[a]}")
            for j in insns[max(0,k-int(sys.argv[3])):k+2]: print(f"   {j.address:08x} {j.mnemonic} {j.op_str}")
