#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Step 1: load nfs3.exe in angr, run CFGFast once, and save
   - db.json   : functions, blocks, call graph, data xrefs, strings (plain data)
Run once (a few minutes); the query tools only read the caches."""
import sys, time, json, pickle, logging, os
sys.setrecursionlimit(100000)
logging.getLogger('angr').setLevel(logging.ERROR)
logging.getLogger('cle').setLevel(logging.ERROR)
logging.getLogger('pyvex').setLevel(logging.ERROR)
import angr

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import EXE, STORE
if not EXE or not os.path.isfile(EXE):
    sys.exit('Set NFSRE_EXE to the path of nfs3.exe (see tools/nfs-re/README.md).')

t = time.time()
p = angr.Project(EXE, auto_load_libs=False)
cfg = p.analyses.CFGFast(normalize=True, data_references=True, cross_references=True,
                         resolve_indirect_jumps=True, force_complete_scan=True,
                         show_progressbar=False)
print('CFG %.0fs: %d functions' % (time.time() - t, len(cfg.kb.functions)))

text_lo, text_hi = 0x401000, 0x534000
funcs = {}
for f in cfg.kb.functions.values():
    if not (text_lo <= f.addr < text_hi):
        continue
    blocks = sorted((b.addr, b.size) for b in f.blocks)
    funcs[f.addr] = dict(name=f.name, blocks=blocks,
                         size=sum(s for _, s in blocks),
                         callees=sorted({c.addr for c in f.functions_called()} if hasattr(f, 'functions_called') else []))
cg = cfg.kb.functions.callgraph
for a in funcs:
    funcs[a]['callees'] = sorted(x for x in cg.successors(a) if x != a or True)

# data references recorded by CFGFast
dxrefs = []
for addr, md in cfg.memory_data.items():
    pass
xr = cfg.kb.xrefs
for dst, refs in xr.xrefs_by_dst.items():
    for r in refs:
        dxrefs.append((r.ins_addr, dst, int(r.type) if r.type is not None else -1))

strings = {}
for addr, md in cfg.memory_data.items():
    if md.sort in ('string', 'unicode') and md.content:
        try:
            s = md.content.decode('latin-1') if isinstance(md.content, bytes) else str(md.content)
        except Exception:
            continue
        strings[addr] = s

db = dict(funcs={hex(a): v for a, v in funcs.items()},
          angr_xrefs=dxrefs,
          angr_strings={hex(a): s for a, s in strings.items()})
json.dump(db, open(os.path.join(STORE, 'cfg_db.json'), 'w'))
print('db written', len(funcs), 'funcs', len(dxrefs), 'xrefs', len(strings), 'strings')
# (pickling the angr Project/kb does not round-trip in angr 10.0.5; decomp.py rebuilds
#  a CFG of just the function's region instead, which takes a few seconds)
