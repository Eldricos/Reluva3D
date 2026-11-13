#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Step 3: build symbols.json from
   - curated.py (hand-curated names, struct layouts, arrays, arg types)
   - every backticked 0x address in Reluva's NFS docs (context lines kept)
   - EA's source-file / function-name strings and the functions that use them."""
import os, sys, re, json, glob
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
import curated as C

DOCS = sorted(glob.glob(os.path.join(REPO, 'docs', 'engines', 'nfs', '**', '*.md'), recursive=True))
D = db()
starts = set(D['func_starts'])
S = D['strings']

out = dict(functions={}, globals={}, code_notes={}, structs={}, arrays={}, ptr_tables={})

def F(a):
    return out['functions'].setdefault('0x%x' % a, {})
def G(a):
    return out['globals'].setdefault('0x%x' % a, {})

# --- 1. docs ---------------------------------------------------------------------
tok = re.compile(r'`(0x[0-9a-fA-F]{6,8})(?:[-–]`?(0x[0-9a-fA-F]{6,8}))?`')
ndoc = 0
for path in DOCS:
    rel = os.path.relpath(path, REPO)
    head = ''
    lines = open(path, encoding='utf-8').read().split('\n')
    for ln, line in enumerate(lines, 1):
        if line.startswith('#'):
            head = line.lstrip('# ').strip()
            continue
        for m in tok.finditer(line):
            a = int(m.group(1), 16)
            if not (0x401000 <= a < 0xa31000):
                continue
            # context: the sentence around the token (or the table row)
            if line.lstrip().startswith('|'):
                ctx = line.strip()
            else:
                para = line
                # widen to the previous/next line for short wraps
                if ln >= 2 and lines[ln - 2].strip() and not lines[ln - 2].startswith('#'):
                    para = lines[ln - 2].strip() + ' ' + para.strip()
                if ln < len(lines) and lines[ln].strip():
                    para = para.strip() + ' ' + lines[ln].strip()
                i = para.find(m.group(0))
                ctx = para[max(0, i - 140): i + 160].strip()
            ref = dict(doc='%s:%d' % (rel, ln), section=head, text=ctx[:300])
            ndoc += 1
            if TEXT[0] <= a < TEXT[1]:
                if a in starts:
                    F(a).setdefault('doc', []).append(ref)
                else:
                    f = func_of(a)
                    ref['func'] = '0x%x' % f if f else None
                    out['code_notes'].setdefault('0x%x' % a, []).append(ref)
            else:
                G(a).setdefault('doc', []).append(ref)
                if m.group(2):
                    G(a)['range_end'] = m.group(2)
print('doc references:', ndoc)

# --- 2. EA strings ---------------------------------------------------------------
ident = re.compile(r'^[A-Za-z_][A-Za-z0-9_]{3,}$')
srcfile = re.compile(r'^[\w:\\./]*\.(c|cpp|C)$')
str_users = defaultdict(set)
for f, lst in D['str_refs'].items():
    for ins, t in lst:
        str_users[t].add(f)
insaddrs = None
nsrc = nname = 0
for f, lst in D['str_refs'].items():
    if f is None:
        continue
    lst = sorted(lst)
    srcs = [(i, t) for i, t in lst if t in S and srcfile.match(S[t].strip())]
    if srcs:
        F(f)['src'] = sorted({S[t].strip() for _, t in srcs})
        nsrc += 1
        # identifier strings referenced within 8 instructions of the file string = function name
        fins = sorted(a for b, l in D['funcs'][f]['blocks'] for a in l)
        pos = {a: k for k, a in enumerate(fins)}
        for i, t in srcs:
            for j, u in lst:
                if u in S and ident.match(S[u]) and abs(pos.get(j, 1e9) - pos.get(i, -1e9)) <= 8 and not srcfile.match(S[u]):
                    F(f).setdefault('ea_names', [])
                    if S[u] not in F(f)['ea_names']:
                        F(f)['ea_names'].append(S[u])
    # trace strings: "name -- message" or EA-style identifiers used by one function only
    for i, t in lst:
        s = S.get(t, '')
        m = re.match(r'^([A-Za-z_]\w{3,})\s*(--|:)', s)
        if m:
            F(f).setdefault('trace', [])
            if s[:80] not in F(f)['trace']:
                F(f)['trace'].append(s[:80])
        if ident.match(s) and len(str_users[t]) == 1 and re.match(r'^(i?SND|[A-Z]{2,}_[a-z]|[a-z]+[A-Z])', s):
            F(f).setdefault('ea_hint', [])
            if s not in F(f)['ea_hint']:
                F(f)['ea_hint'].append(s)
for v in out['functions'].values():
    if v.get('ea_names'):
        nname += 1
print('functions with source file strings:', nsrc, 'named from strings:', nname)

# --- 3. curated -----------------------------------------------------------------
for a, (name, args, desc) in C.FUNCS.items():
    e = F(a)
    e['name'] = name
    e['name_from'] = 'curated'
    if args:
        e['args'] = args
    if desc:
        e['desc'] = desc
for a, (name, ty, desc) in C.GLOBALS.items():
    e = G(a)
    e.update(name=name, type=ty, name_from='curated')
    if desc:
        e['desc'] = desc
# names for the rest
for a, e in out['functions'].items():
    if 'name' not in e:
        if e.get('ea_names'):
            e['name'] = e['ea_names'][0]; e['name_from'] = 'ea_string(src+name)'
        elif e.get('ea_hint') and len(e['ea_hint']) == 1 and re.match(r'^(i?SND|[A-Z]{2,}_[a-z])', e['ea_hint'][0]):
            e['name'] = e['ea_hint'][0]; e['name_from'] = 'ea_string(hint)'
        elif e.get('src'):
            base = re.split(r'[\\/]', e['src'][0])[-1].split('.')[0]
            e['name'] = '%s_%s' % (base, a[2:]); e['name_from'] = 'src_file'
for a, e in out['globals'].items():
    if 'name' not in e and e.get('doc'):
        # table rows like | `0x6fd3b8` | `racetype` | ... give a name
        for r in e['doc']:
            m = re.search(r'`%s`\s*\|\s*`(\w+)`' % re.escape(a), r['text'], re.I)
            if m:
                e['name'] = 'g_' + m.group(1); e['name_from'] = 'doc_table'
                break
# struct layouts
def hx(v):
    return '0x%x' % v
for sn, s in C.STRUCTS.items():
    out['structs'][sn] = dict(size=hx(s['size']), desc=s['desc'], fields={
        hx(o): dict(name=f[0], type=f[1], **({'desc': f[2]} if len(f) > 2 else {})) for o, f in sorted(s['fields'].items())})
for n, (a, st, cnt) in C.ARRAYS.items():
    out['arrays'][n] = dict(addr=hx(a), struct=st, count=cnt)
    G(a).setdefault('name', n)
for n, (a, st, cnt) in C.PTR_TABLES.items():
    out['ptr_tables'][n] = dict(addr=hx(a), struct=st, count=cnt)
    G(a).setdefault('name', n)

json.dump(out, open(os.path.join(STORE, 'symbols.json'), 'w'), indent=1, sort_keys=True)
print('symbols.json: %d functions (%d named), %d globals (%d named), %d code notes' % (
    len(out['functions']), sum('name' in v for v in out['functions'].values()),
    len(out['globals']), sum('name' in v for v in out['globals'].values()), len(out['code_notes'])))
