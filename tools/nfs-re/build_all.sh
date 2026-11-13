#!/bin/sh
# Rebuild every cache (about 5 minutes; only build_cfg.py is slow). Needs
# NFSRE_EXE; the caches go to $NFSRE_DATA (default: build/nfsre in the repo).
set -e
cd "$(dirname "$0")"
PY=${NFSRE_PYTHON:-python3}
STORE=${NFSRE_DATA:-../../build/nfsre}
[ -f "$STORE/cfg_db.json" ] || $PY build_cfg.py   # angr CFGFast, ~4 min (delete cfg_db.json to redo)
$PY build_db.py        # capstone pass, edges, xrefs, strings -> db.pkl (15 s)
$PY make_symbols.py    # curated.py + docs + EA strings -> symbols.json (2 s)
$PY typeflow.py        # struct/constant data flow -> fields.pkl (20 s)
