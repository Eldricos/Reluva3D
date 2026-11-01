#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""nfs3.exe query tool.  nfsre.py <command> ...   (see README.md)
  xref ADDR [--len N]     callers / readers / writers of a function or global
  field STRUCT OFF [--all] every read/write of STRUCT+OFF (type-flow resolved)
  decomp FUNC [--asm]     angr pseudocode with names; --asm: annotated disassembly
  asm FUNC                annotated disassembly
  callers FUNC / callees FUNC
  strings FUNC            strings the function references
  sym NAME|ADDR           what symbols.json knows (docs, names, src file)
  structs [STRUCT]        list struct layouts
  gdecomp FUNC [--fresh]  Ghidra's C for one function (names/types from symbols.json applied)
  gxref ADDR [--len N] [--diff]  Ghidra's references (--diff: against xref's)
ADDR/FUNC: hex (0x4a90a0 or 4a90a0) or a name from symbols.json; an address
inside a function means that function."""
import os, sys, re, pickle, json, bisect, signal
signal.signal(signal.SIGPIPE, signal.SIG_DFL)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *

SY = None
def syms():
    global SY
    if SY is None:
        SY = Syms()
    return SY

_F = None
def fields():
    global _F
    if _F is None:
        _F = pickle.load(open(os.path.join(STORE, 'fields.pkl'), 'rb'))
    return _F

def addr_arg(s):
    s = s.strip()
    if re.fullmatch(r'(0x)?[0-9a-fA-F]+', s):
        return int(s, 16)
    a = syms().lookup_name(s)
    if a is None:
        # struct-array style name  g_cars[0].gear  not supported; try globals
        raise SystemExit('unknown name: %s' % s)
    return a

def func_arg(s):
    a = addr_arg(s)
    D = db()
    if a in D['funcs']:
        return a
    f = func_of(a)
    if f is None:
        raise SystemExit('no function at 0x%x' % a)
    return f

ACC = {1: 'R', 2: 'W', 3: 'RW', 0: '-'}
CONFN = {4: 'direct', 3: 'high', 2: 'medium', 1: 'low'}

def ins_text(a):
    i = db()['insns'].get(a)
    return '%s %s' % (i[1], i[2]) if i else '?'

def fl(a):
    return flabel(a, syms())

def annotate_ins(a, acc_by_ins=None):
    """comment for one instruction: names of globals, fields, call targets, strings"""
    D = db(); S = syms()
    sz, mn, ostr, ops, rw = D['insns'][a]
    notes = []
    for op in ops:
        if op[0] == 'i':
            v = op[1]
            if mn in ('call', 'jmp') and v in D['funcs']:
                n = S.fname(v)
                if n:
                    notes.append(n)
            elif v in D['strings']:
                notes.append(repr(D['strings'][v][:60]))
            elif 0x3a000000 <= v <= 0x4a000000 or 0xba000000 <= v <= 0xca000000:
                notes.append('%gf' % struct.unpack('<f', struct.pack('<I', v))[0])
            else:
                n = S.data_name(v) if v >= DATA[0] and v < DATA[1] else None
                if n:
                    notes.append('&' + n)
        elif op[0] == 'm':
            if not op[1] and not op[2]:
                n = S.data_name(op[4])
                if n:
                    notes.append(n)
                elif op[4] in D['strings']:
                    notes.append(repr(D['strings'][op[4]][:60]))
    if acc_by_ins and a in acc_by_ins:
        for r in acc_by_ins[a]:
            if r[0] == 'field':
                n = S.field_name(r[1], r[2]) if r[2] is not None else None
                notes.append('%s+0x%x%s%s [%s]' % (r[1], r[2] or 0, ' ' + n if n else '', ' (indexed)' if r[4] else '', CONFN[r[3]]))
            elif r[0] == 'abs' and r[3] != 4:
                n = S.data_name(r[1])
                notes.append('-> 0x%x%s (computed)' % (r[1], ' ' + n if n else ''))
    return '; '.join(notes)

def xref_rows(a, ln):
    D = db()
    rows = []
    for t in range(a, a + ln):
        for ins, k, osz in D['xrefs'].get(t, []):
            rows.append((ins, k, osz, t, 'direct', None))
    seen = {(r[0], r[3]) for r in rows}
    # computed accesses (constant-propagated base registers)
    for ins, r, acc, osz, val in fields()['accesses']:
        if r[0] == 'abs' and a <= r[1] < a + ln and (ins, r[1]) not in seen:
            rows.append((ins, {1: 'r', 2: 'w', 3: 'rw'}.get(acc, 'r'), osz, r[1], 'computed', val))
            seen.add((ins, r[1]))
    # angr's xrefs we did not find
    for t in range(a, a + ln):
        for ins, ty in D['angr_xrefs'].get(t, []):
            if (ins, t) not in seen and ins is not None:
                rows.append((ins, {1: 'r', 2: 'w', 0: 'addr'}.get(ty, 'angr'), 0, t, 'angr', None))
                seen.add((ins, t))
    return rows

def cmd_xref(args):
    a = addr_arg(args[0])
    ln = 1
    if '--len' in args:
        ln = int(args[args.index('--len') + 1], 0)
    D = db(); S = syms()
    print('xref %s%s' % (label(a, S), '' if a not in D['funcs'] else '  (function)'))
    dn = S.data_name(a)
    if dn and dn != S.fname(a):
        print('  = %s' % dn)
    rows = xref_rows(a, ln)
    vals = {}
    for ins, r, acc, osz, val in fields()['accesses']:
        if r[0] == 'abs' and a <= r[1] < a + ln and val is not None:
            vals[ins] = val
    order = {'call': 0, 'jmp': 1, 'w': 2, 'rw': 3, 'idx-w': 4, 'idx-rw': 5, 'r': 6, 'idx-r': 7, 'addr': 8, 'idx-addr': 9}
    rows.sort(key=lambda r: (order.get(r[1], 10), r[0]))
    cur = None
    for ins, k, osz, t, how, val in rows:
        grp = {'call': 'CALLERS', 'jmp': 'JUMPS', 'w': 'WRITERS', 'rw': 'WRITERS', 'idx-w': 'WRITERS', 'idx-rw': 'WRITERS',
               'r': 'READERS', 'idx-r': 'READERS', 'addr': 'ADDRESS TAKEN', 'idx-addr': 'ADDRESS TAKEN'}.get(k, k)
        if grp != cur:
            print('%s:' % grp); cur = grp
        v = vals.get(ins, val)
        extra = []
        if t != a:
            extra.append('+0x%x' % (t - a))
        if k.startswith('idx'):
            extra.append('indexed')
        if how != 'direct':
            extra.append(how)
        if v is not None:
            extra.append('value=0x%x' % v)
        print('  0x%06x %-3s %-44s in %s %s' % (ins, k.replace('idx-', '').upper() if k not in ('call', 'jmp', 'addr', 'idx-addr') else '', ins_text(ins)[:44], fl(ins), ' '.join(extra)))
    # pointers to it in data (function tables, task lists)
    if TEXT[0] <= a < TEXT[1]:
        d = raw(); pat = struct.pack('<I', a); i = d.find(pat); first = True
        for n, va, vs, rs, ra in sections():
            pass
        while i >= 0:
            v = None
            for n, va, vs, rs, ra in sections():
                if ra <= i < ra + rs and n != 'AUTO':
                    v = va + i - ra
            if v is not None:
                if first:
                    print('POINTERS IN DATA:'); first = False
                print('  0x%06x %s' % (v, S.data_name(v) or ''))
            i = d.find(pat, i + 1)
    if not rows:
        print('  (no references found)')
    # struct arrays: suggest field
    for base, st, cnt, n in S.arrays:
        sz = S.structs[st]['size']
        if base <= a < base + sz * cnt:
            print('note: inside %s; for all cars/elements use: field %s 0x%x' % (n, st, (a - base) % sz))

def cmd_field(args):
    st = args[0]
    S = syms(); D = db()
    if st not in S.structs:
        raise SystemExit('unknown struct %s (have %s)' % (st, ', '.join(S.structs)))
    if re.fullmatch(r'(0x)?[0-9a-fA-F]+', args[1]):
        off = int(args[1], 16)
    else:
        m = [o for o, f in S.structs[st]['fields'].items() if f['name'] == args[1]]
        if not m:
            raise SystemExit('no field %s in %s' % (args[1], st))
        off = m[0]
    n = S.field_name(st, off)
    print('field %s+0x%x%s' % (st, off, ' (%s)' % n if n else ''))
    # the array field containing off (for indexed accesses)
    arr_start = None
    for o, f in S.structs[st]['fields'].items():
        if o <= off and '[' in f.get('type', '') and S.field_name(st, off) and S.field_name(st, off).startswith(f['name'] + '['):
            arr_start = o
    rows = []
    seen = set()
    for ins, r, acc, osz, val in fields()['accesses']:
        if r[0] != 'field' or r[1] != st:
            continue
        if r[2] == off:
            rows.append((ins, acc, osz, r[3], 'indexed' if r[4] else '', val))
            seen.add(ins)
        elif r[4] and arr_start is not None and r[2] == arr_start:
            rows.append((ins, acc, osz, r[3], 'array %s+0x%x[i]' % (st, arr_start), val))
            seen.add(ins)
    rows.sort(key=lambda r: (0 if r[1] & 2 else 1, r[0]))
    cur = None
    for ins, acc, osz, conf, note, val in rows:
        grp = 'WRITERS' if acc & 2 else 'READERS'
        if grp != cur:
            print('%s:' % grp); cur = grp
        extra = [note] if note else []
        if val is not None:
            extra.append('value=0x%x' % val)
        print('  0x%06x %-2s %d %-44s in %-34s [%s] %s' % (ins, ACC.get(acc, '?'), osz, ins_text(ins)[:44], fl(ins), CONFN[conf], ' '.join(extra)))
    if not rows:
        print('  (no typed accesses)')
    # untyped candidates: same displacement on a base register we could not type
    cand = []
    typed_any = {a for a, r, acc, osz, v in fields()['accesses'] if r[0] == 'field'}
    for a, (sz, mn, ostr, ops, rw) in D['insns'].items():
        if a in seen or a in typed_any or mn == 'lea':
            continue
        for op in ops:
            if op[0] == 'm' and op[4] == off and op[1] and op[1] not in ('ebp', 'esp'):
                cand.append((a, op[6]))
    if cand:
        print('UNRESOLVED [reg+0x%x] (base register type unknown; may be other structs): %d' % (off, len(cand)))
        lim = None if '--all' in args else 25
        cand.sort(key=lambda c: (0 if c[1] & 2 else 1, c[0]))
        for a, acc in cand[:lim]:
            print('  0x%06x %-2s %-44s in %s' % (a, ACC.get(acc, '?'), ins_text(a)[:44], fl(a)))
        if lim and len(cand) > lim:
            print('  ... %d more (--all)' % (len(cand) - lim))
    w = sorted({func_of(r[0]) for r in rows if r[1] & 2})
    print('summary: %d typed accesses (%d writes) in %d functions; writer functions: %s' % (
        len(rows), sum(1 for r in rows if r[1] & 2), len({func_of(r[0]) for r in rows}),
        ', '.join(label(f, S) for f in w) or 'none'))

def acc_index(fa):
    D = db()
    out = {}
    for ins, r, acc, osz, val in fields()['accesses']:
        if D['ins_func'].get(ins) == fa:
            out.setdefault(ins, []).append(r)
    return out

def cmd_asm(args):
    fa = func_arg(args[0])
    D = db(); S = syms()
    f = D['funcs'][fa]
    ai = acc_index(fa)
    tf = fields()['args'].get(fa, {})
    print('; function %s  args(type-flow): %s' % (label(fa, S), ', '.join('%s=%s(%s)' % (r, s, c) for r, (s, c) in tf.items()) or '?'))
    e = S.j['functions'].get('0x%x' % fa, {})
    for k in ('desc', 'src', 'ea_names'):
        if e.get(k):
            print('; %s: %s' % (k, e[k]))
    for d in e.get('doc', [])[:4]:
        print('; doc %s: %s' % (d['doc'], d['text'][:150]))
    notes = S.j.get('code_notes', {})
    starts = {b for b, _ in f['blocks']}
    for b, lst in sorted(f['blocks']):
        print('loc_%x:' % b)
        for a in lst:
            c = annotate_ins(a, ai)
            nd = notes.get('0x%x' % a)
            if nd:
                c = (c + ' | ' if c else '') + 'doc: ' + nd[0]['text'][:90]
            print('  %06x  %-46s %s' % (a, ins_text(a), ('; ' + c) if c else ''))

def cmd_decomp(args):
    fa = func_arg(args[0])
    if '--asm' in args:
        return cmd_asm(args)
    cache = os.path.join(STORE, 'cache', 'decomp_%x.c' % fa)
    cache_raw = cache + '.raw'
    if os.path.exists(cache_raw) and '--fresh' not in args:
        import decomp
        print(decomp.rename(open(cache_raw).read(), fa, syms(), show_types='--types' in args)); return
    if False and os.path.getmtime(cache) > os.path.getmtime(os.path.join(STORE, 'symbols.json')):
        print(open(cache).read()); return
    import decomp
    try:
        text = decomp.decompile(fa, syms())
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        open(cache_raw, 'w').write(text)
        text = decomp.rename(text, fa, syms(), show_types='--types' in args)
    except Exception as e:
        print('// decompiler failed (%s: %s); annotated disassembly follows' % (type(e).__name__, e))
        return cmd_asm(args)
    print(text)

def cmd_callers(args):
    fa = func_arg(args[0])
    D = db(); S = syms()
    print('callers of %s:' % label(fa, S))
    for ins, k, osz in sorted(D['xrefs'].get(fa, [])):
        if k in ('call', 'jmp', 'addr'):
            print('  0x%06x %-4s in %s' % (ins, k, fl(ins)))
    for ins, ty in D['angr_xrefs'].get(fa, []):
        pass

def cmd_callees(args):
    fa = func_arg(args[0])
    D = db(); S = syms()
    print('callees of %s:' % label(fa, S))
    seen = set()
    for b, lst in sorted(D['funcs'][fa]['blocks']):
        for a in lst:
            sz, mn, ostr, ops, rw = D['insns'][a]
            if mn == 'call':
                if ops and ops[0][0] == 'i':
                    print('  0x%06x call %s' % (a, label(ops[0][1], S)))
                else:
                    print('  0x%06x call %s   ; indirect %s' % (a, ostr, annotate_ins(a)))

def cmd_strings(args):
    fa = func_arg(args[0])
    D = db(); S = syms()
    print('strings used by %s:' % label(fa, S))
    for ins, t in sorted(D['str_refs'].get(fa, [])):
        s = D['strings'].get(t)
        if s is None:
            import bisect
            ks = sorted(D['strings']); i = bisect.bisect_right(ks, t) - 1
            s = D['strings'][ks[i]][t - ks[i]:]
        print('  0x%06x -> 0x%06x %r' % (ins, t, s[:120]))

def cmd_sym(args):
    S = syms()
    a = addr_arg(args[0])
    for sect in ('functions', 'globals', 'code_notes'):
        e = S.j[sect].get('0x%x' % a)
        if e:
            print('%s 0x%x:' % (sect, a))
            print(json.dumps(e, indent=1))
    f = func_of(a)
    if f is not None and TEXT[0] <= a < TEXT[1]:
        print('in function %s' % label(f, S))
        tf = fields()['args'].get(f)
        if tf:
            print('arg types (type-flow):', tf)
    dn = S.data_name(a) if DATA[0] <= a < DATA[1] else None
    if dn:
        print('data name:', dn)

def cmd_structs(args):
    S = syms()
    for n, s in S.j['structs'].items():
        if args and n != args[0]:
            continue
        print('%s size %s: %s' % (n, s['size'], s.get('desc', '')))
        if args:
            for o, f in sorted(s['fields'].items(), key=lambda x: int(x[0], 16)):
                print('  +%-6s %-20s %-10s %s' % (o[2:], f['name'], f['type'], f.get('desc', '')))


# --- Ghidra (ghidra_build.sh exports; see README.md) ---------------------------------
GH = os.path.join(STORE, 'ghidra')
_GF = None
def gfuncs():
    """Ghidra's functions: (sorted body ranges [(lo, hi, entry)], {entry: info})"""
    global _GF
    if _GF is None:
        p = os.path.join(GH, 'functions.json')
        if not os.path.exists(p):
            raise SystemExit('no Ghidra export in %s: run tools/nfsre/ghidra_build.sh (README.md, "Ghidra")' % GH)
        j = json.load(open(p))
        fs = {int(k, 16): v for k, v in j.items()}
        rs = sorted((lo, hi, e) for e, v in fs.items() for lo, hi in v['body'])
        _GF = (rs, fs)
    return _GF

def gfunc_of(a):
    rs, fs = gfuncs()
    if a in fs:
        return a
    i = bisect.bisect_right(rs, (a, float('inf'), 0)) - 1
    while i >= 0 and rs[i][0] <= a:
        if a <= rs[i][1]:
            return rs[i][2]
        i -= 1
    return None

def glabel(e):
    if e is None:
        return '(no Ghidra function)'
    n = gfuncs()[1].get(e, {}).get('name', '')
    return '0x%x<%s>' % (e, n) if n and not n.startswith('FUN_') else '0x%x' % e

def symbols_stamp():
    import hashlib
    return hashlib.sha256(open(os.path.join(STORE, 'symbols.json'), 'rb').read()).hexdigest()[:16]

def ghidra_build(*args):
    import subprocess
    sh = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ghidra_build.sh')
    r = subprocess.run([sh] + list(args), stdout=sys.stderr)
    if r.returncode:
        raise SystemExit('ghidra_build.sh failed')

def positional(args):
    p = [x for i, x in enumerate(args) if not x.startswith('--') and not (i and args[i - 1] == '--len')]
    if not p:
        raise SystemExit('give a function or address')
    return p[0]

def cmd_gdecomp(args):
    s = positional(args)
    try:
        a = addr_arg(s)
    except SystemExit:
        m = [e for e, v in gfuncs()[1].items() if v['name'] == s]
        if not m:
            raise
        a = m[0]
    fa = gfunc_of(a)
    if fa is None:
        raise SystemExit('0x%x is in no Ghidra function' % a)
    path = os.path.join(GH, 'decomp', '%x.c' % fa)
    stamp = symbols_stamp()
    if '--fresh' in args or not os.path.exists(path) or open(path).readline().split()[-1] != stamp:
        # not exported yet, or symbols.json changed since: redo this function (the first time after a
        # change this also re-applies symbols.json, about 1.5 min; then about 15 s per function).
        # ghidra_build.sh --apply redoes every function at once.
        print('gdecomp: running Ghidra for 0x%x (no export, symbols.json changed, or --fresh)...' % fa, file=sys.stderr)
        ghidra_build('--only', '0x%x' % fa)
    text = open(path).read()
    print(text.split('\n', 1)[1] if text.startswith('// stamp') else text, end='')

GROUPS = (('CALLERS', ('CALL',)), ('JUMPS', ('JUMP',)), ('WRITERS', ('WRITE',)), ('READERS', ('READ',)),
          ('DATA / ADDRESS TAKEN', ('DATA', 'PARAM', 'INDIRECTION')))
def cmd_gxref(args):
    a = addr_arg(positional(args))
    ln = int(args[args.index('--len') + 1], 0) if '--len' in args else 1
    p = os.path.join(GH, 'xrefs.json')
    if not os.path.exists(p):
        raise SystemExit('no Ghidra export in %s: run tools/nfsre/ghidra_build.sh' % GH)
    X = json.load(open(p))
    S = syms()
    print('gxref %s (Ghidra)' % label(a, S))
    rows = []
    for t in range(a, a + ln):
        for frm, ty, src, op, fe, text in X.get(str(t), []):
            rows.append((frm, ty, src, t, fe, text))
    def grp(ty):
        for g, keys in GROUPS:
            if any(k in ty for k in keys) and not (g == 'READERS' and 'WRITE' in ty):
                return g
        return ty
    order = [g for g, _ in GROUPS]
    rows.sort(key=lambda r: (order.index(grp(r[1])) if grp(r[1]) in order else 9, r[0]))
    cur = None
    for frm, ty, src, t, fe, text in rows:
        g = grp(ty)
        if g != cur:
            print('%s:' % g); cur = g
        extra = []
        if t != a:
            extra.append('+0x%x' % (t - a))
        if src != 'DEFAULT':
            extra.append('computed' if src == 'ANALYSIS' else src.lower())
        print('  0x%06x %-18s %-44s in %s %s' % (frm, ty, text[:44], glabel(fe), ' '.join(extra)))
    if not rows:
        print('  (no references)')
    if '--diff' in args:
        theirs = {r[0] for r in xref_rows(a, ln)}
        ours = {r[0] for r in rows}
        print('only in xref (capstone/type flow/angr): %s' % (' '.join('0x%x' % i for i in sorted(theirs - ours)) or 'none'))
        print('only in Ghidra: %s' % (' '.join('0x%x' % i for i in sorted(ours - theirs)) or 'none'))

CMDS = dict(gdecomp=cmd_gdecomp, gxref=cmd_gxref, xref=cmd_xref, field=cmd_field, decomp=cmd_decomp, asm=cmd_asm, callers=cmd_callers,
            callees=cmd_callees, strings=cmd_strings, sym=cmd_sym, structs=cmd_structs)

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in CMDS:
        print(__doc__); sys.exit(1)
    CMDS[sys.argv[1]](sys.argv[2:])
