# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Shared loading helpers for the nfs3.exe analysis tools."""
import os, sys, json, pickle, struct, bisect
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
# The exe studied (the game's own, never copied into the repo) and where
# the caches derived from it go (by default the build folder, not committed).
EXE = os.environ.get('NFSRE_EXE', '')
STORE = os.environ.get('NFSRE_DATA') or os.path.join(REPO, 'build', 'nfsre')
os.makedirs(STORE, exist_ok=True)
TEXT = (0x401000, 0x534000)
DATA = (0x534000, 0xa19000)   # .idata, DGROUP, .bss

_raw = None
def raw():
    global _raw
    if _raw is None:
        if not EXE or not os.path.isfile(EXE):
            sys.exit('Set NFSRE_EXE to the path of nfs3.exe (see tools/nfs-re/README.md).')
        _raw = open(EXE, 'rb').read()
    return _raw

def sections():
    d = raw()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    ns = struct.unpack_from('<H', d, pe + 6)[0]
    osz = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 52)[0]
    sec = pe + 24 + osz
    out = []
    for i in range(ns):
        n, vs, va, rs, ra = struct.unpack_from('<8sIIII', d, sec + 40 * i)
        out.append((n.rstrip(b'\0').decode(), base + va, vs, rs, ra))
    return out

def v2f(v):
    for n, va, vs, rs, ra in sections():
        if va <= v < va + rs:
            return v - va + ra
    return None

def read(v, n):
    o = v2f(v)
    return raw()[o:o + n] if o is not None else None

def cstr(v, maxlen=200):
    b = read(v, maxlen)
    if not b:
        return None
    b = b.split(b'\0')[0]
    return b.decode('latin-1')

def parse_addr(s, syms=None):
    s = s.strip()
    try:
        return int(s, 16) if s.lower().startswith('0x') or all(c in '0123456789abcdefABCDEF' for c in s) and len(s) >= 5 else int(s, 0)
    except ValueError:
        pass
    if syms:
        a = syms.lookup_name(s)
        if a is not None:
            return a
    raise SystemExit('unknown address/name: %s' % s)

_db = None
def db():
    global _db
    if _db is None:
        with open(os.path.join(STORE, 'db.pkl'), 'rb') as fh:
            _db = pickle.load(fh)
    return _db

class Syms:
    def __init__(self):
        self.j = json.load(open(os.path.join(STORE, 'symbols.json')))
        self.names = {}
        for sect in ('functions', 'globals'):
            for a, v in self.j.get(sect, {}).items():
                if v.get('name'):
                    self.names[int(a, 16)] = v['name']
        self.byname = {v: k for k, v in self.names.items()}
        self.structs = {}
        for sn, s in self.j.get('structs', {}).items():
            self.structs[sn] = dict(size=int(s['size'], 16),
                                    fields={int(o, 16): f for o, f in s['fields'].items()})
        self.arrays = [(int(a['addr'], 16), a['struct'], a['count'], n) for n, a in self.j.get('arrays', {}).items()]
        self.ptrtables = [(int(a['addr'], 16), a['struct'], a['count'], n) for n, a in self.j.get('ptr_tables', {}).items()]

    def lookup_name(self, n):
        return self.byname.get(n)

    def fname(self, a):
        return self.names.get(a)

    def field_name(self, struct, off):
        s = self.structs.get(struct)
        if not s or off is None:
            return None
        fs = s['fields']
        if off in fs:
            return fs[off]['name']
        # inside an array field?
        best = None
        for o, f in fs.items():
            if o < off:
                t = f.get('type', '')
                if '[' in t:
                    import re
                    m = re.match(r'(\w+)\[(\w+)\]', t)
                    if m:
                        esz = {'u8': 1, 's8': 1, 'u16': 2, 's16': 2}.get(m.group(1), 4)
                        n = int(m.group(2), 0)
                        if off < o + esz * n:
                            if best is None or o > best[0]:
                                best = (o, '%s[%d]%s' % (f['name'], (off - o) // esz, '' if (off - o) % esz == 0 else '+%d' % ((off - o) % esz)))
        return best[1] if best else None

    def data_name(self, a):
        """name for a data address: global, array element field, or pointer table slot"""
        if a in self.names:
            return self.names[a]
        for base, st, cnt, n in self.arrays:
            sz = self.structs[st]['size']
            if base <= a < base + sz * cnt:
                i, off = divmod(a - base, sz)
                fn = self.field_name(st, off)
                return '%s[%d].%s' % (n, i, fn or '+0x%x' % off)
        for base, st, cnt, n in self.ptrtables:
            if base <= a < base + 4 * cnt:
                return '%s[%d]' % (n, (a - base) // 4)
        return None

def func_of(addr):
    """containing function start (from db)"""
    D = db()
    f = D['ins_func'].get(addr)
    if f is not None:
        return f
    starts = D['func_starts']
    i = bisect.bisect_right(starts, addr) - 1
    return starts[i] if i >= 0 else None

def label(a, syms):
    n = syms.fname(a) if syms else None
    return '0x%x%s' % (a, ' <%s>' % n if n else '')

def flabel(addr, syms):
    f = func_of(addr)
    if f is None:
        return '?'
    n = syms.fname(f) if syms else None
    return '0x%x%s' % (f, '<%s>' % n if n else '')
