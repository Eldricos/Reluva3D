# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""decomp FUNC: angr decompiler on one function (a CFG of just its region, so a
few seconds), Watcom register calling convention, then names from symbols.json:
functions, globals, and struct types/fields chosen from the type-flow results."""
import os, sys, re, time, logging, pickle
sys.setrecursionlimit(100000)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

ARGREGS = ('eax', 'edx', 'ebx', 'ecx')

def arg_shape(fa):
    """(number of register args, number of stack args) from the entry block and ret imm"""
    D = db()
    f = D['funcs'].get(fa)
    if not f:
        return 1, 0
    blocks = dict(f['blocks'])
    written, used = set(), set()
    lst = blocks.get(fa, ())
    for a in lst:
        sz, mn, ostr, ops, rw = D['insns'][a]
        if mn == 'push':
            continue
        if mn == 'call':
            break
        reads = set()
        for k, op in enumerate(ops):
            if op[0] == 'r' and (op[2] & 1 or (k > 0)):
                reads.add(op[1])
            elif op[0] == 'm':
                reads.update(x for x in (op[1], op[2]) if x)
        if mn == 'xor' and len(ops) == 2 and ops[0] == ops[1]:
            reads = set()
        for r in reads:
            r = {'al': 'eax', 'ax': 'eax', 'dl': 'edx', 'dx': 'edx', 'bl': 'ebx', 'bx': 'ebx', 'cl': 'ecx', 'cx': 'ecx'}.get(r, r)
            if r in ARGREGS and r not in written:
                used.add(r)
        for r in rw:
            written.add(r)
        if ops and ops[0][0] == 'r' and ops[0][2] & 2:
            written.add({'al': 'eax', 'dl': 'edx', 'bl': 'ebx', 'cl': 'ecx'}.get(ops[0][1], ops[0][1]))
    nreg = max([ARGREGS.index(r) + 1 for r in used] or [0])
    nstk = 0
    for b, l in f['blocks']:
        if l:
            sz, mn, ostr, ops, rw = D['insns'][l[-1]]
            if mn == 'ret' and ops and ops[0][0] == 'i':
                nstk = max(nstk, ops[0][1] // 4)
    return nreg, nstk

def angr_structs(syms, arch):
    """SimStruct types for our structs, with padding between the known fields"""
    from angr.sim_type import (SimStruct, SimTypeFixedSizeArray, SimTypeChar, SimTypeShort, SimTypeInt,
                               SimTypeFloat, SimTypePointer, SimTypeBottom)
    from collections import OrderedDict
    base = {'u8': (SimTypeChar, 1), 's8': (SimTypeChar, 1), 'u16': (SimTypeShort, 2), 's16': (SimTypeShort, 2),
            'u32': (SimTypeInt, 4), 's32': (SimTypeInt, 4), 'f32': (SimTypeFloat, 4), 'ptr': (None, 4)}
    out = {}
    for sn in syms.structs:
        out[sn] = SimStruct(OrderedDict(), name=sn, pack=True)
    for sn, s in syms.structs.items():
        flds = OrderedDict()
        pos = 0
        for o in sorted(s['fields']):
            f = s['fields'][o]
            if o < pos:
                continue
            if o > pos:
                flds['pad_%x' % pos] = SimTypeFixedSizeArray(SimTypeChar(), o - pos)
            t = f.get('type', 'u32').rstrip('?')
            m = re.match(r'(\w+)(?::(\w+))?(?:\[(\w+)\])?', t)
            kind, tgt, cnt = m.group(1), m.group(2), m.group(3)
            if kind == 'ptr':
                ty, sz = (SimTypePointer(out[tgt]) if tgt in out else SimTypePointer(SimTypeChar())), 4
            else:
                cls, sz = base.get(kind, (SimTypeInt, 4))
                ty = cls(signed=kind.startswith('s')) if cls in (SimTypeChar, SimTypeShort, SimTypeInt) else cls()
            if cnt:
                n = int(cnt, 0)
                ty, sz = SimTypeFixedSizeArray(ty, n), sz * n
            nm = re.sub(r'\W', '_', f['name'])
            flds[nm] = ty
            pos = o + sz
        if pos < s['size']:
            flds['pad_%x' % pos] = SimTypeFixedSizeArray(SimTypeChar(), s['size'] - pos)
        out[sn].fields = flds
    for v in out.values():
        v.with_arch(arch)
    return {k: v.with_arch(arch) for k, v in out.items()}

def decompile(fa, syms, show_asm_on_fail=True):
    logging.disable(logging.CRITICAL)
    import angr
    from angr.calling_conventions import SimCC, SimRegArg, SimStackArg
    from angr.sim_type import SimTypeFunction, SimTypeInt, SimTypePointer, SimTypeBottom
    from angr.knowledge_plugins.functions.function import PrototypeSource
    from angr.enums import Flavors

    class SimCCWatcom(SimCC):
        ARG_REGS = ['eax', 'edx', 'ebx', 'ecx']
        FP_ARG_REGS = []
        RETURN_VAL = SimRegArg('eax', 4)
        RETURN_ADDR = SimStackArg(0, 4)
        STACKARG_SP_DIFF = 4
        CALLEE_CLEANUP = True
        ARCH = None

    D = db()
    p = angr.Project(EXE, auto_load_libs=False)
    fb = D['funcs'][fa]['blocks']
    lo = min(b for b, _ in fb)
    hi = max(l[-1] + D['insns'][l[-1]][0] for b, l in fb if l)
    cfg = p.analyses.CFGFast(regions=[(lo, hi)], function_starts=[fa], normalize=True,
                             data_references=True, show_progressbar=False)
    kb = cfg.kb
    if fa not in kb.functions:
        raise RuntimeError('angr found no function at 0x%x' % fa)
    targs = {}
    try:
        F = pickle.load(open(os.path.join(STORE, 'fields.pkl'), 'rb'))
        targs = F['args']
    except Exception:
        pass
    stypes = angr_structs(syms, p.arch)
    for g in list(kb.functions.values()):
        nreg, nstk = arg_shape(g.addr)
        g.calling_convention = SimCCWatcom(p.arch)
        ta = targs.get(g.addr, {})
        nreg = max([nreg] + [ARGREGS.index(r) + 1 for r in ta if r in ARGREGS])
        al = []
        for k in range(nreg + nstk):
            r = ARGREGS[k] if k < len(ARGREGS) and k < nreg else None
            if r in ta and ta[r][0] in stypes:
                al.append(SimTypePointer(stypes[ta[r][0]]))
            else:
                al.append(SimTypeInt())
        names = [ta[ARGREGS[k]][0] if k < nreg and ARGREGS[k] in ta else 'a%d' % k for k in range(nreg + nstk)]
        proto = SimTypeFunction(al, SimTypeInt(), arg_names=names).with_arch(p.arch)
        try:
            g.set_prototype(Flavors.DEFAULT_FLAVOR, proto, source=PrototypeSource.USER)
        except Exception:
            g.prototype = proto
        n = syms.fname(g.addr)
        if n:
            g.name = n
    f = kb.functions[fa]
    dec = p.analyses.Decompiler(f, kb=kb, cfg=cfg.model)
    if not dec.codegen:
        raise RuntimeError('decompiler produced no code')
    return dec.codegen.text

def struct_evidence(fa):
    """{struct: set(offsets)} accessed in this function, from fields.pkl"""
    F = pickle.load(open(os.path.join(STORE, 'fields.pkl'), 'rb'))
    D = db()
    ev = {}
    for a, r, acc, osz, v in F['accesses']:
        if r[0] == 'field' and D['ins_func'].get(a) == fa:
            ev.setdefault(r[1], set()).add(r[2])
    return ev, F['args'].get(fa, {})

def rename(text, fa, syms, show_types=False):
    ev, args = struct_evidence(fa)
    # 1. angr's struct typedefs: offsets -> pick our struct
    tdefs = {}
    for m in re.finditer(r'typedef struct (\w+) \{(.*?)\} \1;', text, re.S):
        name, body = m.group(1), m.group(2)
        fields = {}
        for fm in re.finditer(r'^\s*(.+?)\s*\*?(field|padding)_([0-9a-f]+)(\[\d+\])?;', body, re.M):
            fields[int(fm.group(3), 16)] = (fm.group(2), fm.group(1).strip().rstrip('*').strip())
        tdefs[name] = fields
    mapping = {}
    def score(fields, S):
        offs = {o for o, (k, t) in fields.items() if k == 'field'}
        if not offs or max(offs) >= syms.structs[S]['size']:
            return -1
        return len(offs & ev.get(S, set())) * 10 + len(offs & set(syms.structs[S]['fields']))
    changed = True
    while changed:
        changed = False
        for name, fields in tdefs.items():
            if name in mapping:
                continue
            best = max(((score(fields, S), S) for S in syms.structs), default=(-1, None))
            if best[0] >= 2:
                mapping[name] = best[1]; changed = True
        # pointer fields: car.field_528 -> carp
        for name, fields in tdefs.items():
            if name in mapping:
                for o, (k, t) in fields.items():
                    t2 = t.replace('struct', '').strip()
                    if t2 in tdefs and t2 not in mapping:
                        f = syms.structs[mapping[name]]['fields'].get(o)
                        if f and f.get('type', '').startswith('ptr:'):
                            mapping[t2] = f['type'][4:]; changed = True
    # 2. field references: walk  var->field_x->field_y  chains
    decl = {}
    for m in re.finditer(r'^\s*(?:struct\s+)?(\w+) \*+(v\d+|a\d+)\b', text, re.M):
        decl[m.group(2)] = m.group(1)
    def fld(tname, off):
        S = mapping.get(tname)
        return (S, syms.field_name(S, off)) if S else (None, None)
    def chain(m):
        var, rest = m.group(1), m.group(2)
        t = decl.get(var)
        out = var
        for fm in re.finditer(r'->(field|padding)_([0-9a-f]+)(\[[^\]]*\])?', rest):
            kind, off = fm.group(1), int(fm.group(2), 16)
            S, n = fld(t, off) if t else (None, None)
            if kind == 'field' and n and '[' not in n:
                out += '->' + n
            elif kind == 'field' and n:
                out += '->' + n.replace('[', '_').replace(']', '').replace('+', '_')
            else:
                tag = fm.group(0)[2:]
                out += '->' + tag + (' /*%s+0x%x %s*/' % (S, off, n or '') if S else '')
            nt = tdefs.get(t, {}).get(off, (None, None))[1] if t else None
            t = nt.replace('struct', '').strip() if nt else None
        return out
    text = re.sub(r'\b(v\d+|a\d+)((?:->(?:field|padding)_[0-9a-f]+(?:\[[^\]]*\])?)+)', chain, text)
    # typedef bodies: rename fields
    def tdef(m):
        name, body = m.group(1), m.group(2)
        S = mapping.get(name)
        if not S:
            return m.group(0)
        def fl(fm):
            n = syms.field_name(S, int(fm.group(2), 16))
            return fm.group(0) if not n or fm.group(1) != 'field' else n.replace('[', '_').replace(']', '').replace('+', '_')
        body = re.sub(r'\b(field|padding)_([0-9a-f]+)\b', fl, body)
        return 'typedef struct %s {%s} %s;' % (name, body, name)
    text = re.sub(r'typedef struct (\w+) \{(.*?)\} \1;', tdef, text, flags=re.S)
    for name, S in mapping.items():
        text = re.sub(r'\b%s\b' % name, S, text)
    # 3. globals and functions
    def g(m):
        a = int(m.group(1), 16)
        n = syms.data_name(a) or syms.fname(a)
        return n if n else m.group(0)
    text = re.sub(r'\bg_([0-9a-f]{6,8})\b', g, text)
    text = re.sub(r'\bsub_([0-9a-f]{6,8})\b', g, text)
    # integer literals that are float bit patterns (x87 compares done as int compares)
    def fl(m):
        v = int(m.group(0), 0) & 0xffffffff
        if 0x3a000000 <= v <= 0x4a000000 or 0xba000000 <= v <= 0xca000000:
            import struct as _s
            f = _s.unpack('<f', _s.pack('<I', v))[0]
            return '%s /*%gf*/' % (m.group(0), f)
        return m.group(0)
    text = re.sub(r'(?<![\w.])(0x[0-9a-fA-F]{8}|\d{9,10})(?![\w.])', fl, text)
    if not show_types:
        text = re.sub(r'typedef struct (\w+) \{.*?\} \1;\n*', '', text, flags=re.S)
    e = syms.j['functions'].get('0x%x' % fa, {})
    docs = ''.join('// doc %s: %s\n' % (d['doc'], d['text'][:160]) for d in e.get('doc', [])[:3])
    if e.get('desc'):
        docs = '// %s\n' % e['desc'] + docs
    hdr = docs + '// 0x%x %s   (angr decompiler; struct types: %s; args: %s)\n' % (
        fa, syms.fname(fa) or '', ', '.join('%s=%s' % kv for kv in mapping.items()) or 'none',
        ', '.join('%s:%s(%s)' % (r, s, c) for r, (s, c) in args.items()) or '?')
    return hdr + text
