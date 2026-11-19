# Clean-room reading aid for Clue.exe: strings, references to addresses, disassembly.
import os
GAME = os.environ.get('CLUE_GAME', 'game')  # the installed game: Clue.exe, the DLLs, Data/
WORK = os.environ.get('CLUE_WORK', '.')  # caches and outputs
import sys, re, pefile, capstone, pickle, os
EXE = os.path.join(GAME, 'Clue.exe')
pe = pefile.PE(EXE); base = pe.OPTIONAL_HEADER.ImageBase
def secs():
    for s in pe.sections: yield s.Name.rstrip(b'\0').decode(), base + s.VirtualAddress, s.get_data()
cache = os.path.join(WORK, 'insns.pkl')
def insns():
    if os.path.exists(cache): return pickle.load(open(cache, 'rb'))
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.skipdata = True
    out = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(text.get_data(), base + text.VirtualAddress)]
    pickle.dump(out, open(cache, 'wb')); return out
cmd = sys.argv[1]
if cmd == 'str':  # str <regex>: strings matching, with their addresses
    pat = re.compile(sys.argv[2].encode(), re.I)
    for n, va, d in secs():
        if n == '.text': continue
        for m in re.finditer(rb'[\x20-\x7e]{3,}', d):
            if pat.search(m.group()): print(hex(va + m.start()), n, m.group().decode())
elif cmd == 'ref':  # ref <hexaddr> [before] [after]: instructions using that value
    a = int(sys.argv[2], 16); b = int(sys.argv[3]) if len(sys.argv) > 3 else 3; f = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    I = insns(); pat = re.compile(r'\b0x%x\b' % a)
    for k, (ad, mn, op) in enumerate(I):
        if pat.search(op):
            print('---', hex(ad))
            for j in I[max(0, k - b):k + f + 1]: print('   %08x %s %s' % j)
elif cmd == 'dis':  # dis <hexaddr> <count>
    a = int(sys.argv[2], 16); I = insns(); k = next(i for i, x in enumerate(I) if x[0] >= a)
    for j in I[k:k + int(sys.argv[3])]: print('   %08x %s %s' % j)
elif cmd == 'calls':  # calls <hexaddr>: callers
    a = int(sys.argv[2], 16); I = insns(); n = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    for k, (ad, mn, op) in enumerate(I):
        if mn in ('call', 'jmp') and op == hex(a):
            print('---', hex(ad))
            for j in I[max(0, k - n):k + 1]: print('   %08x %s %s' % j)
elif cmd == 'dword':  # dword <hexaddr> [n]: dwords in data
    a = int(sys.argv[2], 16)
    for n, va, d in secs():
        if va <= a < va + len(d):
            o = a - va; import struct
            print([hex(x) for x in struct.unpack_from('<%dI' % int(sys.argv[3] if len(sys.argv) > 3 else 8), d, o)])
if cmd == 'func':  # func <hexaddr>: start of the function holding it (after the previous nop/ret run)
    a = int(sys.argv[2], 16); I = insns(); k = next(i for i, x in enumerate(I) if x[0] >= a)
    while k > 0 and not (I[k-1][1] in ('nop', 'int3') or I[k-1][1].startswith('ret') or I[k-1][1] == 'jmp' and I[k][1] == 'push' and False): k -= 1
    print(hex(I[k][0]))
