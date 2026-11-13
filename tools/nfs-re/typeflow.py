#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Step 4: struct-pointer and constant data flow over every function.

Abstract values (per register, per [ebp+d] stack slot, and a small push/pop stack):
  ('c', v)                 a constant (addresses of globals, tables)
  ('p', S, delta, conf)    a pointer into struct S; delta = offset from the
                           struct start, None when a computed index was added
  ('s', k)                 an integer known to be scaled by k (imul/shl), for
                           array indexing (i * 0x9ac + g_cars)
Seeds: curated function argument types (eax = car ...), struct arrays
(g_cars + i*0x9ac), pointer tables ([i*4 + g_list_racers]), pointer fields
(car+0x528 -> carp).  Argument types are then inferred from call sites
(a register holding a struct pointer at >= half of the direct calls and
no conflicting struct) and the whole program iterated.
Writes fields.pkl: every memory access with its resolved struct/offset or
absolute address, confidence and the constant stored (if known)."""
import os, sys, re, pickle, time, json
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

CONF = {'high': 3, 'medium': 2, 'low': 1}
CONFN = {3: 'high', 2: 'medium', 1: 'low'}
R32 = {'al': 'eax', 'ah': 'eax', 'ax': 'eax', 'bl': 'ebx', 'bh': 'ebx', 'bx': 'ebx',
       'cl': 'ecx', 'ch': 'ecx', 'cx': 'ecx', 'dl': 'edx', 'dh': 'edx', 'dx': 'edx',
       'si': 'esi', 'di': 'edi', 'bp': 'ebp', 'sp': 'esp'}
GPR = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')
ARGREGS = ('eax', 'edx', 'ebx', 'ecx')     # Watcom register calling convention

def r32(r):
    return R32.get(r, r) if r else None

def load_model():
    sy = Syms()
    structs = {n: s for n, s in sy.structs.items()}
    ptrfields = {}   # (struct, off) -> struct
    for sn, s in structs.items():
        for o, f in s['fields'].items():
            t = f.get('type', '')
            if t.startswith('ptr:'):
                ptrfields[(sn, o)] = t[4:]
    arrays = [(b, st, cnt, structs[st]['size']) for b, st, cnt, n in sy.arrays]
    ptabs = [(b, st, cnt) for b, st, cnt, n in sy.ptrtables]
    gptr = {}
    for a, g in sy.j['globals'].items():
        t = g.get('type', '') or ''
        if t.startswith('ptr:'):
            gptr[int(a, 16)] = t[4:]
    args = {}
    for a, f in sy.j['functions'].items():
        if f.get('args'):
            args[int(a, 16)] = {r: (s, 'high') for r, s in f['args'].items()}
    return sy, structs, ptrfields, arrays, ptabs, gptr, args

SY, STRUCTS, PTRF, ARRAYS, PTABS, GPTR, CUR_ARGS = load_model()

def in_array(a):
    for b, st, cnt, sz in ARRAYS:
        if b <= a < b + cnt * sz:
            return b, st, sz
    return None

def ptab_at(a):
    for b, st, cnt in PTABS:
        if b <= a < b + 4 * cnt:
            return b, st
    return None

def join(a, b):
    if a == b:
        return a
    if a is None or b is None:
        return None
    if a[0] == 'p' and b[0] == 'p' and a[1] == b[1]:
        return ('p', a[1], a[2] if a[2] == b[2] else None, min(a[3], b[3]))
    return None

def join_state(s1, s2):
    regs = {}
    for k in set(s1[0]) & set(s2[0]):
        v = join(s1[0][k], s2[0][k])
        if v is not None:
            regs[k] = v
    slots = {}
    for k in set(s1[1]) & set(s2[1]):
        v = join(s1[1][k], s2[1][k])
        if v is not None:
            slots[k] = v
    stk = s1[2] if s1[2] == s2[2] else ()
    return (regs, slots, stk)

def addr_value(st, base, idx, scale, disp):
    """abstract value of base + idx*scale + disp"""
    regs = st[0]
    bv = regs.get(base) if base else None
    iv = regs.get(idx) if idx else None
    if base and base in ('esp',):
        return None
    if bv is None and base is None and iv is None and idx is None:
        return ('c', disp)
    # struct pointer base
    if bv and bv[0] == 'p':
        S = bv[1]
        if idx is None:
            return ('p', S, None if bv[2] is None else bv[2] + disp, bv[3])
        if iv and iv[0] == 's' and iv[1] * scale == STRUCTS[S]['size'] and bv[2] is not None:
            return ('p', S, bv[2] + disp, bv[3])
        return ('p', S, None, bv[3])
    if iv and iv[0] == 'p' and scale == 1:
        return addr_value(st, idx, base, 1, disp)
    # constant base
    c = None
    if bv and bv[0] == 'c':
        c = bv[1] + disp
    elif base is None:
        c = disp
    if c is not None:
        c &= 0xffffffff
        if idx is None:
            return ('c', c)
        arr = in_array(c)
        if arr:
            b, S, sz = arr
            if iv and iv[0] == 's' and iv[1] * scale == sz:
                return ('p', S, (c - b) % sz, CONF['high'])
            return ('p', S, None, CONF['medium']) if (c - b) % sz == 0 else None
        return None
    # unknown base + constant disp inside a struct array:  [reg + 0x5e162b] with reg = i*0x9ac
    if idx is None:
        arr = in_array(disp)
        if arr:
            b, S, sz = arr
            if bv and bv[0] == 's' and bv[1] == sz:
                return ('p', S, (disp - b) % sz, CONF['high'])
            if bv is None and disp - b >= 0:
                return ('p', S, (disp - b) % sz, CONF['low'])
    return None

def load_value(st, mem, osz):
    """value loaded from memory operand"""
    _, base, idx, scale, disp, _, _ = mem
    base, idx = r32(base), r32(idx)
    if base == 'ebp' and idx is None and osz == 4:
        return st[1].get(disp)
    if osz != 4:
        return None
    av = addr_value(st, base, idx, scale, disp)
    if av is None:
        # pointer table indexed: [i*4 + table]
        if base is None or st[0].get(base) is None:
            pt = ptab_at(disp) if (idx or base) and scale in (1, 4) else None
            if pt:
                return ('p', pt[1], 0, CONF['high'] if scale == 4 else CONF['medium'])
        return None
    if av[0] == 'c':
        pt = ptab_at(av[1])
        if pt:
            return ('p', pt[1], 0, CONF['high'])
        if av[1] in GPTR:
            return ('p', GPTR[av[1]], 0, CONF['high'])
        return None
    if av[0] == 'p' and av[2] is not None:
        t = PTRF.get((av[1], av[2]))
        if t:
            return ('p', t, 0, av[3])
    return None

def mem_access_record(st, mem):
    """(kind, struct-or-addr, off, conf, indexed) for a memory operand, or None"""
    _, base, idx, scale, disp, osz, acc = mem
    base, idx = r32(base), r32(idx)
    if base in ('ebp', 'esp') and idx is None:
        return None
    regs = st[0]
    bv = regs.get(base) if base else None
    iv = regs.get(idx) if idx else None
    if bv and bv[0] == 'a' and idx is None:
        return ('arg', bv[1], disp, 1, False)
    if bv and bv[0] == 'p' or (iv and iv[0] == 'p' and scale == 1 and not (bv and bv[0] == 'p')):
        if not (bv and bv[0] == 'p'):
            bv, iv, idx = iv, bv, base
        S = bv[1]
        exact_idx = idx is None or (iv and iv[0] == 's' and iv[1] * scale == STRUCTS[S]['size'])
        if bv[2] is None:
            return ('field', S, disp, bv[3], True)
        return ('field', S, bv[2] + disp, bv[3], not exact_idx)
    av = addr_value(st, base, idx, scale, disp)
    if av is None:
        return None
    if av[0] == 'c':
        return ('abs', av[1], None, CONF['high'] if base else 4, False)
    if av[0] == 'p':
        if av[2] is None:
            arr = in_array(disp)
            if arr:
                return ('field', av[1], (disp - arr[0]) % arr[2], av[3], True)
            return None
        return ('field', av[1], av[2], av[3], False)
    return None

STK_MAX = 16

def step(st, a, ins, rec, calls_out, fa):
    """transfer function; st is mutated copy. rec(...) records accesses."""
    sz, mn, ostr, ops, rw = ins
    regs, slots, stk = st
    # record memory accesses (before the update)
    if mn != 'lea' and not mn.startswith('nop'):
        for op in ops:
            if op[0] == 'm':
                r = mem_access_record(st, op)
                if r:
                    # value stored, when it is a constant
                    val = None
                    if op[6] & 2 and mn == 'mov' and len(ops) == 2:
                        src = ops[1]
                        if src[0] == 'i':
                            val = src[1]
                        elif src[0] == 'r':
                            v = regs.get(r32(src[1]))
                            if v and v[0] == 'c':
                                val = v[1]
                    rec(a, r, op[6], op[5], mn, val)
    dst = ops[0] if ops else None
    def setr(r, v):
        r = r32(r)
        if r == 'esp':
            return
        if v is None:
            regs.pop(r, None)
        else:
            regs[r] = v
    def val(op):
        if op[0] == 'r':
            return regs.get(r32(op[1])) if op[1] in GPR or op[1] in ('esp',) else None
        if op[0] == 'i':
            return ('c', op[1])
        if op[0] == 'm':
            return load_value(st, op, op[5])
        return None
    full = lambda op: op[0] == 'r' and op[1] in GPR
    def symv(r, at):
        """value of r; an unknown integer gets a symbol so i*4-i (= 3i) can be followed"""
        r = r32(r)
        v = regs.get(r)
        if v is None and r in GPR and r != 'esp':
            v = ('s', 1, (at, r))
            regs[r] = v
        return v
    if mn == 'mov' and dst:
        if dst[0] == 'r':
            setr(dst[1], val(ops[1]) if full(dst) else None)
        elif dst[0] == 'm':
            _, base, idx, sc, disp, osz, acc = dst
            if r32(base) == 'ebp' and idx is None:
                v = val(ops[1]) if osz == 4 else None
                if v is None:
                    slots.pop(disp, None)
                else:
                    slots[disp] = v
        return
    if mn == 'lea' and dst and dst[0] == 'r':
        m = ops[1]
        b_, i_ = r32(m[1]), r32(m[2])
        v = addr_value(st, b_, i_, m[3], m[4])
        if v is None and m[4] == 0 and i_ in GPR and (b_ is None or b_ == i_):
            iv = symv(i_, a)
            if iv[0] == 's':
                v = ('s', iv[1] * (m[3] + (1 if b_ else 0)), iv[2])
        setr(dst[1], v)
        return
    if mn in ('add', 'sub') and dst and full(dst):
        a1 = regs.get(dst[1])
        b1 = val(ops[1])
        if a1 and a1[0] == 's' and ops[1][0] == 'r' and full(ops[1]):
            b1 = symv(ops[1][1], a)
            if b1[0] == 's' and b1[2] == a1[2] and a1[2] is not None:
                setr(dst[1], ('s', a1[1] + (b1[1] if mn == 'add' else -b1[1]), a1[2]))
                return
        if mn == 'sub':
            if a1 and b1 and b1[0] == 'c' and a1[0] == 'c':
                setr(dst[1], ('c', (a1[1] - b1[1]) & 0xffffffff))
            elif a1 and a1[0] == 'p' and b1 and b1[0] == 'c':
                setr(dst[1], ('p', a1[1], None if a1[2] is None else a1[2] - b1[1], a1[3]))
            else:
                setr(dst[1], None)
            return
        if a1 and b1 and a1[0] == 'c' and b1[0] == 'c':
            setr(dst[1], ('c', (a1[1] + b1[1]) & 0xffffffff))
            return
        for x, y in ((a1, b1), (b1, a1)):
            if x and x[0] == 'p':
                if y and y[0] == 'c':
                    setr(dst[1], ('p', x[1], None if x[2] is None else x[2] + y[1], x[3]))
                elif y and y[0] == 's' and y[1] == STRUCTS[x[1]]['size']:
                    setr(dst[1], x)
                else:
                    setr(dst[1], ('p', x[1], None, x[3]))
                return
            if x and x[0] == 'c':
                arr = in_array(x[1])
                if arr:
                    b, S, ssz = arr
                    if y and y[0] == 's' and y[1] == ssz:
                        setr(dst[1], ('p', S, (x[1] - b) % ssz, CONF['high']))
                    elif (x[1] - b) % ssz == 0:
                        setr(dst[1], ('p', S, None, CONF['medium']))
                    else:
                        setr(dst[1], None)
                    return
        setr(dst[1], None)
        return
    if mn == 'imul' and dst and full(dst):
        if len(ops) == 3 and ops[2][0] == 'i':
            src = symv(ops[1][1], a) if full(ops[1]) else ('s', 1, None)
            setr(dst[1], ('s', ops[2][1] * src[1], src[2]) if src[0] == 's' else None)
        elif len(ops) == 2 and ops[1][0] == 'i':
            src = symv(dst[1], a)
            setr(dst[1], ('s', ops[1][1] * src[1], src[2]) if src[0] == 's' else None)
        else:
            setr(dst[1], None)
        return
    if mn == 'shl' and dst and full(dst) and len(ops) == 2 and ops[1][0] == 'i':
        v = regs.get(dst[1])
        k = 1 << ops[1][1]
        if v is None:
            v = symv(dst[1], a)
        setr(dst[1], ('s', v[1] * k, v[2]) if v[0] == 's' else ('c', (v[1] << ops[1][1]) & 0xffffffff) if v[0] == 'c' else None)
        return
    if mn == 'xor' and len(ops) == 2 and ops[0][0] == 'r' and ops[1][0] == 'r' and ops[0][1] == ops[1][1] and full(dst):
        setr(dst[1], ('c', 0))
        return
    if mn == 'xchg' and len(ops) == 2 and full(ops[0]) and full(ops[1]):
        x, y = regs.get(ops[0][1]), regs.get(ops[1][1])
        setr(ops[0][1], y); setr(ops[1][1], x)
        return
    if mn == 'push':
        v = val(ops[0]) if ops and ops[0][0] != 'm' or (ops and ops[0][5] == 4) else None
        st2 = (stk + (v,))[-STK_MAX:]
        st[2] = st2
        return
    if mn == 'pop':
        if stk:
            v = stk[-1]; st[2] = stk[:-1]
        else:
            v = None
        if ops and ops[0][0] == 'r':
            setr(ops[0][1], v if full(ops[0]) else None)
        return
    if mn == 'call':
        tgt = ops[0][1] if ops and ops[0][0] == 'i' else None
        calls_out.append((a, tgt, {r: regs.get(r) for r in ARGREGS}))
        # Watcom: callee saves everything but eax (and edx for 64-bit results)
        regs.pop('eax', None)
        return
    if mn in ('pushal', 'pushad'):
        st[2] = (stk + (None,) * 8)[-STK_MAX:]
        return
    if mn in ('popal', 'popad'):
        st[2] = stk[:-8] if len(stk) >= 8 else ()
        return
    # default: kill written registers
    for r in rw:
        if r in GPR:
            regs.pop(r, None)
    if dst and dst[0] == 'r' and r32(dst[1]) in GPR and dst[2] & 2:
        regs.pop(r32(dst[1]), None)

def analyse_function(fa, f, insns, entry_args, rec, calls_out):
    blocks = {b: lst for b, lst in f['blocks']}
    edges = f['edges']
    entry = {r: ('a', r) for r in ARGREGS}
    entry.update({r: ('p', s, 0, CONF[c]) for r, (s, c) in entry_args.items()})
    IN = {b: None for b in blocks}
    if fa in blocks:
        IN[fa] = (entry, {}, ())
    order = sorted(blocks)
    while True:
        roots = [b for b in order if IN[b] is not None]
        work = list(roots)
        inwork = set(work)
        cnt = defaultdict(int)
        while work:
            b = work.pop(0); inwork.discard(b)
            cnt[b] += 1
            s = IN[b]
            st = [dict(s[0]), dict(s[1]), s[2]]
            for a in blocks[b]:
                step(st, a, insns[a], lambda *x: None, [], fa)
            out = (st[0], st[1], st[2])
            for nb in edges.get(b, []):
                if nb not in IN:
                    continue
                new = out if IN[nb] is None else join_state(IN[nb], out)
                if new != IN[nb]:
                    IN[nb] = new
                    if nb not in inwork and cnt[nb] < 30:
                        work.append(nb); inwork.add(nb)
        # blocks nothing reached (jump-table cases, handlers): start them empty
        un = [b for b in order if IN[b] is None]
        if not un:
            break
        IN[un[0]] = ({}, {}, ())
    # final pass: record
    for b in sorted(blocks):
        s = IN[b]
        st = [dict(s[0]), dict(s[1]), s[2]]
        for a in blocks[b]:
            step(st, a, insns[a], rec, calls_out, fa)

def run():
    t0 = time.time()
    D = db()
    insns, funcs = D['insns'], D['funcs']
    args = {fa: dict(v) for fa, v in CUR_ARGS.items()}
    inferred_from = {}
    for it in range(12):
        accesses = []
        calls = []
        def rec(a, r, acc, osz, mn, val):
            accesses.append((a, r, acc, osz, val))
        for fa, f in funcs.items():
            co = []
            analyse_function(fa, f, insns, args.get(fa, {}), rec, co)
            calls.extend(co)
        # infer arguments from call sites
        votes = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))
        ncalls = defaultdict(int)
        for a, tgt, av in calls:
            if tgt is None or tgt not in funcs:
                continue
            ncalls[tgt] += 1
            for r, v in av.items():
                if v and v[0] == 'p' and v[2] == 0:
                    votes[tgt][r][v[1]] += 1
        changed = 0
        for tgt, rv in votes.items():
            for r, sv in rv.items():
                if len(sv) != 1:
                    continue
                (S, n), = sv.items()
                if n * 2 >= ncalls[tgt] and r not in args.get(tgt, {}):
                    args.setdefault(tgt, {})[r] = (S, 'medium')
                    inferred_from[(tgt, r)] = (n, ncalls[tgt])
                    changed += 1
        # functions whose untyped argument is used like a car (car+0x528, or 2+ car-only fields)
        caroff = {o for o in STRUCTS['car']['fields'] if o >= 0x4ac}
        uses = defaultdict(set)
        for a, r, acc, osz, v in accesses:
            if r[0] == 'arg':
                uses[(D['ins_func'].get(a), r[1])].add(r[2])
        for (fa, reg), offs in uses.items():
            if fa is None or reg in args.get(fa, {}):
                continue
            if 0x528 in offs or len(offs & caroff) >= 2:
                args.setdefault(fa, {})[reg] = ('car', 'low')
                inferred_from[(fa, reg)] = ('usage', sorted(offs & caroff | ({0x528} & offs))[:6])
                changed += 1
        accesses = [x for x in accesses if x[1][0] != 'arg']
        print('iteration %d: %d accesses, %d new argument types (%.0fs)' % (it, len(accesses), changed, time.time() - t0))
        if not changed:
            break
    pickle.dump(dict(accesses=accesses, args=args, inferred=inferred_from, calls=calls),
                open(os.path.join(STORE, 'fields.pkl'), 'wb'), protocol=pickle.HIGHEST_PROTOCOL)
    print('fields.pkl written (%.0fs)' % (time.time() - t0))

if __name__ == '__main__':
    run()
