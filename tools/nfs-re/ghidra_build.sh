#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Ghidra pipeline for nfs3.exe (see README.md, "Ghidra").  Needs GHIDRA_HOME
# (the unpacked ghidra_11.4.2_PUBLIC folder), NFSRE_EXE and symbols.json from
# make_symbols.py.  Everything goes to $NFSRE_DATA/ghidra (not the repo):
#   project nfs3.gpr/.rep, decomp/<addr>.c, functions.json, xrefs.json, applied.stamp
#
#   ghidra_build.sh             import + auto-analysis the first time (about 1.5 min),
#                               then apply symbols.json and export everything
#   ghidra_build.sh --apply     apply symbols.json again and re-export (after curated.py
#                               changes; no re-analysis)
#   ghidra_build.sh --only ADDR...  re-export just these functions (applying first if
#                               symbols.json changed); this is what `gdecomp --fresh` runs
#   ghidra_build.sh --reimport  delete the project and start again
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
STORE=${NFSRE_DATA:-$HERE/../../build/nfsre}
mkdir -p "$STORE"
STORE=$(cd "$STORE" && pwd)
OUT=$STORE/ghidra
SYMS=$STORE/symbols.json
: "${GHIDRA_HOME:?set GHIDRA_HOME to the unpacked Ghidra 11.4.2 folder (see tools/nfs-re/README.md)}"
[ -f "$SYMS" ] || { echo "no $SYMS: run make_symbols.py (build_all.sh) first" >&2; exit 1; }
MEM=${GHIDRA_MAXMEM:-6G}
# analyzeHeadless with a larger heap than its fixed 2G
headless() {
	"$GHIDRA_HOME/support/launch.sh" fg jdk Ghidra-Headless "$MEM" "" \
		ghidra.app.util.headless.AnalyzeHeadless "$OUT" nfs3 "$@"
}
log() { tee -a "$OUT/ghidra.log" | grep -E 'nfsre |ERROR|REPORT|Exception|added the' || true; }

mode=all
case "$1" in
	--apply) mode=apply; shift ;;
	--only) mode=only; shift ;;
	--reimport) rm -rf "$OUT/nfs3.gpr" "$OUT/nfs3.rep"; shift ;;
esac
STAMP=$(sha256sum "$SYMS" | cut -c1-16)
mkdir -p "$OUT"
: > "$OUT/ghidra.log"

if [ ! -f "$OUT/nfs3.gpr" ]; then
	[ -n "$NFSRE_EXE" ] && [ -f "$NFSRE_EXE" ] || { echo "set NFSRE_EXE to nfs3.exe" >&2; exit 1; }
	echo "importing and analysing $NFSRE_EXE (about 1.5 min)"
	headless -import "$NFSRE_EXE" -scriptPath "$HERE/ghidra" -preScript NfsreFixBss.java -max-cpu "$(nproc)" -analysisTimeoutPerFile 3600 2>&1 | log
	rm -f "$OUT/applied.stamp"
	mode=all
fi

# angr's function starts (from db.pkl, if built) seed the functions Ghidra's analysis misses
if [ -f "$STORE/db.pkl" ]; then
	NFSRE_DATA="$STORE" "${NFSRE_PYTHON:-python3}" -c "import sys; sys.path.insert(0, sys.argv[1]); from common import db
print('\n'.join('0x%x' % a for a in db()['func_starts']))" "$HERE" > "$OUT/angr_starts.txt"
fi
APPLY=""
if [ "$mode" != only ] || [ "$(cat "$OUT/applied.stamp" 2>/dev/null)" != "$STAMP" ]; then
	APPLY="-postScript NfsreApply.java $SYMS $OUT/angr_starts.txt"
fi
if [ "$mode" = only ]; then
	[ $# -gt 0 ] || { echo "--only needs function addresses" >&2; exit 1; }
	headless -process nfs3.exe -noanalysis -scriptPath "$HERE/ghidra" $APPLY \
		-postScript NfsreExport.java "$OUT" "$STAMP" "$@" 2>&1 | log
else
	rm -rf "$OUT/new"
	headless -process nfs3.exe -noanalysis -scriptPath "$HERE/ghidra" $APPLY \
		-postScript NfsreExport.java "$OUT/new" "$STAMP" 2>&1 | log
fi
grep -q 'nfsre export done' "$OUT/ghidra.log" || { echo "Ghidra run failed, see $OUT/ghidra.log" >&2; exit 1; }
[ -z "$APPLY" ] || grep -q 'nfsre apply done' "$OUT/ghidra.log" || { echo "apply failed, see $OUT/ghidra.log" >&2; exit 1; }
[ -z "$APPLY" ] || echo "$STAMP" > "$OUT/applied.stamp"
if [ "$mode" != only ]; then   # the full export replaces the old one only when it worked
	rm -rf "$OUT/decomp" && mv "$OUT/new/decomp" "$OUT/decomp"
	mv "$OUT/new/functions.json" "$OUT/new/xrefs.json" "$OUT/" && rmdir "$OUT/new"
fi
