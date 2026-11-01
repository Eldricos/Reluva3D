#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Step 2: disassemble every function found by CFGFast (cfg_db.json) with
capstone and save db.pkl: instructions with operands, functions, intra-function
edges, call graph, absolute data xrefs, strings and who references them."""
import os, sys, json, pickle, re, time
from collections import defaultdict
import capstone as cs
from capstone import x86 as X
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

t0 = time.time()
cfgdb = json.load(open(os.path.join(STORE, 'cfg_db.json')))
md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
md.detail = True
d = raw()

def reg32(r):
    """capstone reg name -> containing 32-bit register name"""
    if not r:
        return None
    r = r.lower()
    m = {'al': 'eax', 'ah': 'eax', 'ax': 'eax', 'bl': 'ebx', 'bh': 'ebx', 'bx': 'ebx',
         'cl': 'ecx', 'ch': 'ecx', 'cx': 'ecx', 'dl': 'edx', 'dh': 'edx', 'dx': 'edx',
         'si': 'esi', 'di': 'edi', 'bp': 'ebp', 'sp': 'esp'}
    return m.get(r, r)

FPU_STORE = {'fst', 'fstp', 'fist', 'fistp', 'fisttp', 'fbstp', 'fnstsw', 'fstsw', 'fnstcw', 'fstcw',
             'fnsave', 'fsave', 'fnstenv', 'fstenv'}
insns = {}      # addr -> (size, mnem, opstr, ops, regs_written)
ins_func = {}
funcs = {}
for fs, fv in cfgdb['funcs'].items():
    fa = int(fs, 16)
    blocks = []
    for ba, bs in fv['blocks']:
        code = read(ba, bs)
        if code is None:
            continue
        lst = []
        for i in md.disasm(code, ba):
            if i.address not in insns:
                ops = []
                for op in i.operands:
                    acc = op.access
                    if op.type == X.X86_OP_MEM and i.mnemonic in FPU_STORE:
                        acc = 2   # capstone reports x87 stores as reads
                    if op.type == X.X86_OP_REG:
                        ops.append(('r', md.reg_name(op.reg), acc))
                    elif op.type == X.X86_OP_IMM:
                        ops.append(('i', op.imm & 0xffffffff, acc))
                    elif op.type == X.X86_OP_MEM:
                        m = op.mem
                        ops.append(('m', md.reg_name(m.base) if m.base else None,
                                    md.reg_name(m.index) if m.index else None,
                                    m.scale, m.disp & 0xffffffff, op.size, acc))
                try:
                    rr, rw = i.regs_access()
                    rw = tuple(sorted({reg32(md.reg_name(r)) for r in rw}))
                except Exception:
                    rw = ()
                insns[i.address] = (i.size, i.mnemonic, i.op_str, tuple(ops), rw)
            ins_func.setdefault(i.address, fa)
            lst.append(i.address)
        blocks.append((ba, tuple(lst)))
    funcs[fa] = dict(name=fv['name'], blocks=blocks, callees=[int(x) for x in fv['callees']])
print('disasm %.0fs: %d insns, %d funcs' % (time.time() - t0, len(insns), len(funcs)))

# intra-function successor edges, computed from the last instruction of each block
JCC = re.compile(r'^(j(?!mp)\w+|loop\w*|jecxz)$')
for fa, f in funcs.items():
    starts = {b for b, _ in f['blocks']}
    edges = {}
    targeted = set()
    indirect = []
    for b, lst in f['blocks']:
        if not lst:
            edges[b] = []
            continue
        last = lst[-1]
        sz, mn, ostr, ops, rw = insns[last]
        nxt = last + sz
        succ = []
        if mn == 'jmp':
            if ops and ops[0][0] == 'i':
                succ = [ops[0][1]]
            else:
                indirect.append(b)
        elif JCC.match(mn):
            if ops and ops[0][0] == 'i':
                succ.append(ops[0][1])
            succ.append(nxt)
        elif mn in ('ret', 'retn', 'iret', 'hlt', 'ud2'):
            succ = []
        else:
            succ = [nxt]
        succ = [s for s in succ if s in starts]
        targeted.update(succ)
        edges[b] = succ
    if indirect:   # jump tables: give their state to blocks no other edge reaches
        orphans = [b for b, _ in f['blocks'] if b not in targeted and b != fa]
        for b in indirect:
            edges[b] = orphans
    f['edges'] = edges

# call graph
callers = defaultdict(set)
for fa, f in funcs.items():
    for c in f['callees']:
        callers[c].add(fa)

# --- xrefs from instructions ---------------------------------------------------
# kinds: call jmp r w rw addr (immediate = address taken)  ; idx-* = with base/index regs
xrefs = defaultdict(list)    # target -> [(ins, kind, size)]
def inrange(v):
    return TEXT[0] <= v < DATA[1]
for a, (sz, mn, ostr, ops, rw) in insns.items():
    for op in ops:
        if op[0] == 'i' and inrange(op[1]):
            if mn == 'call':
                xrefs[op[1]].append((a, 'call', 0))
            elif mn.startswith('j') or mn.startswith('loop'):
                xrefs[op[1]].append((a, 'jmp', 0))
            else:
                xrefs[op[1]].append((a, 'addr', 0))
        elif op[0] == 'm':
            _, base, idx, sc, disp, osz, acc = op
            if mn == 'lea':
                if inrange(disp):
                    xrefs[disp].append((a, 'addr' if not (base or idx) else 'idx-addr', osz))
                continue
            if not inrange(disp) or disp < DATA[0] and (base or idx):
                continue
            k = {1: 'r', 2: 'w', 3: 'rw'}.get(acc, 'r')
            if mn in ('call', 'jmp'):
                k = 'r'
            if base or idx:
                k = 'idx-' + k
            xrefs[disp].append((a, k, osz))

# --- strings ---------------------------------------------------------------------
strings = {}
for n, va, vs, rs, ra in sections():
    if n not in ('DGROUP', 'AUTO'):
        continue
    blob = d[ra:ra + rs]
    for m in re.finditer(rb'[\t\n\r -~]{4,}\x00', blob):
        strings[va + m.start()] = m.group()[:-1].decode('latin-1')
for k, v in cfgdb.get('angr_strings', {}).items():
    strings.setdefault(int(k, 16), v)
# strings referenced by code (start or inside: a reference to the middle counts too)
skeys = sorted(strings)
import bisect
str_refs = defaultdict(list)    # func -> [(ins, straddr)]
for tgt, lst in xrefs.items():
    i = bisect.bisect_right(skeys, tgt) - 1
    if i < 0:
        continue
    s0 = skeys[i]
    if s0 <= tgt < s0 + len(strings[s0]) + 1:
        for ins, k, _ in lst:
            if k in ('addr', 'idx-addr', 'r'):
                f = ins_func.get(ins)
                str_refs[f].append((ins, tgt))

# angr's own data xrefs (may include computed ones)
angr_x = defaultdict(list)
for ins, dst, ty in cfgdb.get('angr_xrefs', []):
    angr_x[dst].append((ins, ty))

D = dict(insns=insns, ins_func=ins_func, funcs=funcs, callers={k: sorted(v) for k, v in callers.items()},
         xrefs=dict(xrefs), strings=strings, str_refs=dict(str_refs), angr_xrefs=dict(angr_x),
         func_starts=sorted(funcs))
pickle.dump(D, open(os.path.join(STORE, 'db.pkl'), 'wb'), protocol=pickle.HIGHEST_PROTOCOL)
print('db.pkl written %.0fs' % (time.time() - t0))
