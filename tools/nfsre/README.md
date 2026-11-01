# nfs3.exe static-analysis tools

Query tools for Reluva's clean-room study of `nfs3.exe` (October-November
2026): cross-references, every access to a struct's field, and decompiled
routines with the names the docs give them. No game data is in the repo:
the tools read your own copy of the exe, and the caches they derive from it
go to the build folder.

## Setting up

    python3 -m venv ~/nfsre-venv && ~/nfsre-venv/bin/pip install -r tools/nfsre/requirements.txt
    export NFSRE_PYTHON=~/nfsre-venv/bin/python
    export NFSRE_EXE="/path/to/Need For Speed III/nfs3.exe"
    export NFSRE_DATA=build/nfsre                 # optional; this is the default
    tools/nfsre/build_all.sh                      # about 5 minutes, once
    export PATH=$PWD/tools/nfsre/bin:$PATH

`curated.py` is the hand-made name map: functions, globals and struct
layouts (the car record, `carp.txt` in memory, the voices, the network
records...). Add to it as things are learned, then run `make_symbols.py` and
`typeflow.py` again (or `build_all.sh`, which skips the slow first step when
its cache is there). `make_symbols.py` also takes every backticked address in
`docs/formats/nfs/*.md` and `docs/engines/nfs*.md`, so the docs feed the map.

## Commands

    xref    0x6fd50c            # callers/readers/writers (+ values stored when constant)
    xref    0x5efc48 --len 64   # a whole table
    field   car 0x5b0           # every read/write of car+0x5b0, via type flow
    field   car gear            # field by name; field carp 0x1f8 (array element)
    decomp  0x4a90a0            # angr pseudocode with struct/field/global names
    decomp  car_engine --asm    # annotated disassembly instead (also the fallback)
    asm     0x4ad672            # annotated disassembly
    callers 0x4a90a0 ; callees 0x4ad7a3 ; strings 0x4f5280
    sym     0x4a2830            # what the docs/strings/curation say about an address
    structs car                 # struct layout

A FUNC or ADDR can be hex (`0x4a90a0` or `4a90a0`) or a name from symbols.json.
An address inside a function means the function that contains it. Queries take
about 1.5 s. The first `decomp` of a function takes 5-10 s and is then cached in
`cache/` (the renaming is applied again each time). Use `--fresh` to run it again.

## Files and pipeline (`./build_all.sh`)

| Step | File | Makes |
|---|---|---|
| 1 | `build_cfg.py` | angr 10.0.5 CFGFast (about 4 min) -> `cfg_db.json`: about 9.8k functions, blocks, call graph, angr's xrefs and strings |
| 2 | `build_db.py` | capstone pass -> `db.pkl`: 394k instructions with operands and read/write access (x87 stores fixed), intra-function edges, direct data xrefs, strings and the functions that use them |
| 3 | `make_symbols.py` | `symbols.json`, from `curated.py` (edit this one), every backticked address in `docs/formats/nfs/*.md` and `docs/engines/nfs*.md` (with context lines), and EA's strings (source file `cmn\connect.c` plus the function-name string next to it, e.g. `CONN_restore`; trace messages) |
| 4 | `typeflow.py` | `fields.pkl`: struct-pointer and constant data flow per function, through registers, `[ebp+d]` slots and push/pop. Seeds are curated argument types, struct arrays (`g_cars` 0x5e1120 x 0x9ac, `g_voices` 0xa09b3c x 0x60...), pointer tables (`[i*4+0x5efc48]`) and pointer fields (car+0x528 -> carp). It follows `i*0x9ac + base` and `(i*4-i)<<5`. Argument types are inferred from call sites (medium confidence) and from usage (low: car+0x528 or 2+ car-only offsets), repeated to a fixpoint |

The confidence of each access is one of `direct` (absolute address), `high`
(curated seed or exact array index), `medium` (inferred from callers or a
computed array index) or `low` (inferred from usage). `indexed` means a computed
index was added to the pointer, e.g. `P + 0x1F0 + 4*i`. `field` also lists
`UNRESOLVED [reg+off]`: accesses at the same displacement whose base register
could not be typed. Check those by hand. They may be other structs.

## Limits

- Function boundaries come from angr. Some interior blocks are split off as
  "functions" (e.g. `0x4a8bb9`, `0x4ac5ab`), and their entry state is unknown.
  This is why some accesses come out unresolved or as `low`.
- Only `[ebp+d]` stack slots are tracked. esp-relative slots, memory aliasing and
  globals that hold pointers (unless curated with `ptr:` types) are not.
- Calls are assumed to keep every register but eax (Watcom convention).
- angr's decompiler gives argument counts from a prologue heuristic.
  Array-relative names can come out shifted: `inv_top_speed[6 + n]` is
  `mps_per_rpm[n - 2]`. Float constants compared as integers get a
  `/*0.9f*/` comment.
- radare2 5.5 is installed (`r2`) as a fallback, but the tools do not use it.
