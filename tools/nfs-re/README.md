# nfs3.exe static-analysis tools

Query tools for Reluva's clean-room study of `nfs3.exe` (October-November
2026): cross-references, every access to a struct's field, and decompiled
routines with the names the docs give them. No game data is in the repo:
the tools read your own copy of the exe, and the caches they derive from it
go to the build folder.

## Setting up

    python3 -m venv ~/nfsre-venv && ~/nfsre-venv/bin/pip install -r tools/nfs-re/requirements.txt
    export NFSRE_PYTHON=~/nfsre-venv/bin/python
    export NFSRE_EXE="/path/to/Need For Speed III/nfs3.exe"
    export NFSRE_DATA=build/nfsre                 # optional; this is the default
    tools/nfs-re/build_all.sh                      # about 5 minutes, once
    export PATH=$PWD/tools/nfs-re/bin:$PATH

`curated.py` is the hand-made name map: functions, globals and struct
layouts (the car record, `carp.txt` in memory, the voices, the network
records...). Add to it as things are learned, then run `make_symbols.py` and
`typeflow.py` again (or `build_all.sh`, which skips the slow first step when
its cache is there). `make_symbols.py` also takes every backticked address in
the pages in `docs/engines/nfs/`, so the docs feed the map.

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

    gdecomp 0x4a90a0            # Ghidra's C, names, struct types and Watcom signatures applied
    gdecomp car_engine --fresh  # run Ghidra again for this one function
    gxref   0x6fd50c            # Ghidra's references (+ pointers in data, constant-propagated ones)
    gxref   0x6fd50c --diff     # ... and which ones `xref` finds that Ghidra does not, and back

A FUNC or ADDR can be hex (`0x4a90a0` or `4a90a0`) or a name from symbols.json.
An address inside a function means the function that contains it. Queries take
about 1.5 s. The first `decomp` of a function takes 5-10 s and is then cached in
`cache/` (the renaming is applied again each time). Use `--fresh` to run it again.

## Files and pipeline (`./build_all.sh`)

| Step | File | Makes |
|---|---|---|
| 1 | `build_cfg.py` | angr 10.0.5 CFGFast (about 4 min) -> `cfg_db.json`: about 9.8k functions, blocks, call graph, angr's xrefs and strings |
| 2 | `build_db.py` | capstone pass -> `db.pkl`: 394k instructions with operands and read/write access (x87 stores fixed), intra-function edges, direct data xrefs, strings and the functions that use them |
| 3 | `make_symbols.py` | `symbols.json`, from `curated.py` (edit this one), every backticked address in the pages in `docs/engines/nfs/` (with context lines), and EA's strings (source file `cmn\connect.c` plus the function-name string next to it, e.g. `CONN_restore`; trace messages) |
| 4 | `typeflow.py` | `fields.pkl`: struct-pointer and constant data flow per function, through registers, `[ebp+d]` slots and push/pop. Seeds are curated argument types, struct arrays (`g_cars` 0x5e1120 x 0x9ac, `g_voices` 0xa09b3c x 0x60...), pointer tables (`[i*4+0x5efc48]`) and pointer fields (car+0x528 -> carp). It follows `i*0x9ac + base` and `(i*4-i)<<5`. Argument types are inferred from call sites (medium confidence) and from usage (low: car+0x528 or 2+ car-only offsets), repeated to a fixpoint |

The confidence of each access is one of `direct` (absolute address), `high`
(curated seed or exact array index), `medium` (inferred from callers or a
computed array index) or `low` (inferred from usage). `indexed` means a computed
index was added to the pointer, e.g. `P + 0x1F0 + 4*i`. `field` also lists
`UNRESOLVED [reg+off]`: accesses at the same displacement whose base register
could not be typed. Check those by hand. They may be other structs.

## Ghidra

Ghidra's analysis and decompiler, with the same names and types, from
`ghidra_build.sh`. Get Ghidra 11.4.2 (it needs a Java 21 JDK) and unpack it
anywhere outside the repo:

    curl -LO https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.4.2_build/ghidra_11.4.2_PUBLIC_20250826.zip
    unzip ghidra_11.4.2_PUBLIC_20250826.zip
    export GHIDRA_HOME=$PWD/ghidra_11.4.2_PUBLIC
    tools/nfs-re/ghidra_build.sh              # after build_all.sh; about 4 minutes the first time

Everything goes to `$NFSRE_DATA/ghidra`: the project (`nfs3.gpr`, `nfs3.rep`,
about 30 MB), `decomp/<addr>.c` for every function, `functions.json`,
`xrefs.json`, `ghidra.log`. The heap is 6 GB (`GHIDRA_MAXMEM` to change it).

| Step | Script | Does | Time |
|---|---|---|---|
| import (first run only) | `ghidra/NfsreFixBss.java` (pre-script), then auto-analysis | Watcom writes `.bss` with no virtual size and a raw size; Ghidra's loader maps it from file offset 0, cut at the end of the file, so `.bss` held the exe's own bytes and stopped at 0x6f2000 (no `0x6fd50c`...). The pre-script makes it an uninitialized block of its full 0x4aa400 bytes | 1.5 min |
| apply | `ghidra/NfsreApply.java symbols.json angr_starts.txt` | functions at angr's starts that Ghidra has not got (about 4,900: code reached only through pointer tables, a quarter of the code) and auto-analysis of them; a `__watcall` prototype (eax, edx, ebx, ecx, then stack; callee pops; every register but eax kept) added as a program specification extension; the structs in `/nfsre` (overlapping curated fields become comments, arrays running into the next field are cut), applied to `g_cars` (car[16]), the other struct arrays, the pointer tables (car*[16]) and the typed globals; function and global names; the signatures (below) | 1.7 min |
| export | `ghidra/NfsreExport.java` | decompiles all 8.5k functions (parallel) and writes the references | 1 min |

Ghidra has no Watcom convention, and its ordered parameter lists cannot say "stack
arguments without the four registers" (`fast_hypot` takes two floats on the stack),
so the apply step sets each function's parameters itself: it decompiles every
function, keeps the inputs it reads (eax, edx, ebx, ecx, and the registers below the
last one used, then stack slots), and commits them with custom storage. Callers
then pass exactly those. It repeats this for the callers of functions whose
arguments changed (a function passing its edx on to a callee is seen once the callee
takes edx), up to 8 rounds. A function that never sets eax gets a void result, or an
x87 one (`float10` in st0) when its callers use st0 right after the call. Curated
argument types (`car *` in eax) replace the recovered ones. The return value is
otherwise left to the decompiler.

Re-running: after `curated.py` (or the docs) change, run `make_symbols.py`, then
`ghidra_build.sh --apply` (about 2.6 minutes, no re-analysis). It replaces what the
last apply did: names it set that are gone are removed, the structs are redefined in
place, every signature is derived again. `gdecomp` checks each file's stamp (a hash of
`symbols.json`); when it is out of date it runs `ghidra_build.sh --only ADDR` for that
function (the first one after a change also re-applies, about 2 minutes; then about
7 s each; `--fresh` forces it). From the export, `gdecomp` takes under 0.1 s and
`gxref` 0.2 s (1.5 s with `--diff`).
`--reimport` starts again from the exe.

How it compares with angr's `decomp` (checked on `0x4a90a0`, `0x4ad672`, `0x4ad6eb`):

- The same logic. Ghidra's is shorter: no dead stores (angr computes the rpm of the
  next gear up and never uses it), real `if`/`else` instead of `goto`s, `void`
  results where angr returns whatever eax holds, `ROUND()` for `fistp`, and struct
  fields through pointers (`car->carp->redline`) where angr has the same.
- Signatures: Ghidra has `fast_hypot(|vel_lat|, |speed|)` returning `float10`; angr
  shows `fast_hypot(v6, v8)` with register garbage. Argument counts agree with
  `decomp.py`'s prologue heuristic for the ones checked (`sound_play` 4+2,
  `car_queue_sound` 4+3, `task_add` 4, the car tasks 1).
- Both compare floats as integers where the code does (`(int)local_54 < 0x3d4ccccd`
  is `< 0.05f`); Ghidra gives no `/*0.05f*/` comment. Both show the array-shift
  `inv_top_speed[num_gears + 6]` (`mps_per_rpm[n - 2]`). Double constants in DGROUP
  show as `(float)_DAT_0053dcac` (the data is writable, so they are not folded).
- `0x4ad6eb` flips the sign with `*(byte *)(&road_speed + 3) ^= 0x80` in both.

`gxref` against `xref`: for `0x6fd50c` the same 33 instructions (the 11 writers
0x43f43e, 0x43f451, 0x43f45e, 0x43f927, 0x452d57, 0x452f34, 0x4726b0, 0x472b32,
0x49e0ac, 0x4a663a, 0x4c3fc6 and 22 readers). Some function starts differ
(Ghidra: 0x452d32, 0x440469, 0x44efc8, 0x452ab4; angr: 0x452d40, 0x440470,
0x44efd5, 0x452ac0). Over all named globals: 2,425 references in both, 16 only in
`xref` (angr's guesses, mostly `[reg+0x2bc]`-style operands, and table bases where
Ghidra resolved the element: `[eax*4+0x571124]` at 0x40a510 is 0x5711b4 there),
37 only in Ghidra (pointers in data tables such as the front-end options rows at
0x5563fc, and constant-propagated operands).

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
- Ghidra: about 130 of the functions made at angr's starts are interior pieces of
  larger ones (angr's splits, e.g. inside `carp_parse`, where Ghidra's own function
  stops at a jump table it did not resolve); they decompile with `unaff_EBP`. Two
  functions do not decompile (0x4fe1d0, 0x51a187). Calls into Windows/DirectX
  clobber ecx/edx, which shows as `extraout_ECX` in callers.
