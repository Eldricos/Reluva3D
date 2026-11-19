# Need for Speed III: 3D Device Setup's card data (3ddata.dat, 3DDATA.EXE)

> **Status:** partial · **Code:** `src/engines/nfs/host/game_host.cpp` (none: not read) · **Checked on:** the 3D Device Setup files, compared with what the program writes · **Confidence:** from the code unless marked

Documented, not needed by Reluva. Neither the game nor its
renderer DLLs read these files; only the install's 3D Device Setup
program does, to choose which renderer DLL the game is told to load.
Reluva tells the game its renderer itself (`GameHost`'s registry, "Thrash
Driver"), so nothing here is implemented.

The install's `3dSetup` folder holds 3D Device Setup (`3dsetup.exe` and
`3dsetupm.exe`, Microsoft's compiler with MFC, 1998-08-12), its card
data (`3ddata.dat`, 435 bytes), `3ddata.exe` (61,761 bytes, Watcom,
1998-08-12), `d3dtest.dll` and a `d3da.dll`. The Classic CD's
`Patch/Voodoo3` folder has a newer `3ddata.dat` (465 bytes) and
`3DDATA.EXE` (86,062 bytes, Microsoft's compiler, 1999-01-30), beside its
`voodoo2a.dll` ([thrash.md](../systems/thrash.md), The Voodoo3 patch's
voodoo2a.dll). Its readme says to run `3DDATA.EXE` ("it appears that
nothing happens"), copy `3ddata.dat` over the install's, copy the DLL,
then pick the Voodoo3 in 3D Device Setup.

## Sources and checks

November 2026, from the files alone (nothing was run): the CD and
install copies, `pefile` for imports and sections, `capstone` for the
code, with small scripts kept outside the repo (the session's work
folder). No 3dfx or EA source was used.

- **Who opens `3ddata.dat`.** Its name was searched for in every program
  and DLL of the install and the CD: only `3dsetup.exe` and
  `3dsetupm.exe` have it (four times each). `nfs3.exe`, `softtria.dll`,
  `d3da.dll`, `voodooa.dll`, both `voodoo2a.dll`s, `eacsnd.dll` and
  `d3dtest.dll` have neither the file's name nor its keys ("regname",
  "CARD_", "Triple Buffer", "D3D-3Dfx") nor the registry key the
  `3ddata` programs write ("3D Data"). Neither `3ddata` program can
  open a file: the CD's imports no `CreateFileA` or `ReadFile` at all,
  and the install's code has no reference to the name.
- **`3dsetup.exe`'s readers** were followed from the four references to
  the name (`0x402766`, `0x404f46`, `0x405544`, `0x4059c8`): each opens
  "3ddata.dat" relative to the current folder with MFC's text file class
  and reads it a line at a time.
- **The `3ddata` programs** were read whole (the CD's `0x401000`-
  `0x401c1a`, one function filling a table and one writing the
  registry), and their tables dumped from the pointers the code indexes
  (the CD's names at `0x40f030`, modules `0x40f12c`, groups `0x40f180`,
  type counts `0x40f1d4`, vendor and device IDs `0x40f228`/`0x40f570`;
  the install's at `0x409840` onwards, found by the pointer to its first
  name).

## 3ddata.dat

Plain text, lines ended by CR LF (the last without one), no header, no
section names. Four kinds of line, each found by its own pass over the
file:

- **`product=<name>`**: the first line whose first eight characters
  match `product=` without regard to case (the C library's `_stricmp`,
  `0x40c3c0`) gives the product's name; it is put after the window's
  title, with ": " between (`0x401769`-`0x4017ef`). The install's copy
  writes the key in lower case, the patch's with a capital; both work.
  No such line: "Error! Invalid data file." and the program ends.
- **`regname=<key>`**: the same, for the game's own registry key under
  `HKEY_LOCAL_MACHINE\SOFTWARE\` (both copies name the game's
  "Electronic Arts\Need For Speed III", the key `nfs3.exe` reads its
  "Thrash Driver" from).
- **`CARD_<card name>[ /G:<group>][ /M:<module>]`** (`0x404f46`): the
  first five characters must be `CARD_` exactly (`strcmp`). The card's
  name runs to " /" or the end of the line; `/G:` and `/M:` each take the
  text up to the next space. Every name goes into a list of the cards
  the game supports; a `/G:` gives a group, and a `/M:` a renderer
  module, to put in place of the registry's for that card.
- **`<switch>=<digit>`** (`0x405544`): any other line is split at its
  first `=`; the key is compared exactly with five names, and the value's
  first character less '0', not zero, sets the switch. `Triple Buffer`
  is copied to the game's key as its "Triple Buffer" value; `D3D` lets
  cards whose module is `d3d` be offered (below); `D3D-3Dfx` adds a
  second entry, "<name> [Direct 3D]" on `d3d`, for the Voodoo Graphics,
  Voodoo2 and Voodoo Rush only (by name, `0x4037e8`-`0x40383a`);
  `D3D-PowerVR` does the same for PowerVR; `Test` is read at start-up
  (`0x4018e3`, beside the card check; not followed further: the
  program's performance test is `d3dtest.dll`'s `D3DTEST_TestCard`). A
  missing switch stays off.

The install's copy has twelve card lines (the patch's thirteen): 3dfx's Voodoo Graphics, Voodoo2,
Voodoo Rush and Banshee with no options, and eight other makers' cards
each with `/M:d3d`; all switches are 0 but `D3D` in the install's copy.
The software renderer has no line: 3D Device Setup adds it itself after
the cards (`0x403a32`).

**The patch's copy** differs in three ways, and only these: a first card
line for "3Dfx Voodoo3" with `/M:voodoo2` (the Voodoo3 on
`voodoo2a.dll`, the patch DLL); `D3D` 0 where the install's has 1; and
`Product` with a capital. With `D3D` off, the cards the registry gives
the module `d3d` (below) are no longer offered at all; the two
cards whose registry module is their own (`rage`, `verite`) still pass
that test, since it looks at the registry's module before the `/M:`
override (`0x4031d9`, then `0x403581`-`0x4037b3`). The readme's "3dfx
Voodoo3 appears twice" is not this file's doing: `D3D-3Dfx` is off in
both, and its second entry is only for the three names above.

## What 3D Device Setup does with it

`0x40298e`: it opens `HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\3D
Data` (written by the `3ddata` programs, below), reads "Cards" and the
"Sort Order" values, and for each card in that order reads its
`Card<n>` key ("Name", "Module", "Group", "Types"). A card is a candidate
only if its "Name" is exactly one of the file's `CARD_` names, and, when
its module is `d3d`, only with `D3D` on. For each candidate it then reads
the `Type<n>` keys' "VendorID" and "DeviceID" and looks for that PCI
device on the machine (`0x403d49`: `HKEY_LOCAL_MACHINE\Enum\PCI`'s
`VEN_xxxx&DEV_xxxx` keys, or `detectnt.dll`'s `PCICheckVendorDevice`,
by the look of it on NT: `0x402720` asks `GetVersionExA` for the NT
platform). A card found is listed, its module and
group replaced by the file's `/M:` and `/G:` when it has them. What the
user picks is written to the game's key (`0x404a1f`-`0x404daf`): "3D
Device Description", "3D Card", "Thrash Driver" (the module: the name
the game looks up in its renderer table, `voodoo2` for `voodoo2a.dll`,
docs/engines/nfs/games/nfs3/PLAN.md), "Group", "D3D Device", "Triple Buffer" and
"Hardware Acceleration" (set by whether the choice is the software
renderer, `0x404d65`). `nfs3.exe` reads "Thrash Driver", "D3D Device" and "3D Device
Description" of these.

So the patch's `3ddata.dat` is what lets 3D Device Setup offer a
Voodoo3 at all (the install's has no line for it, so the card isn't a
candidate even when detected), and it sends the Voodoo3 to
`voodoo2a.dll`, the file the patch replaces.

## 3DDATA.EXE and 3ddata.exe

Each writes a fixed card table, compiled in, into the registry, and
shows a message box only on failure ("couldn't create main key"):

- `HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\3D Data`: "Version"
  (DWORD), "Cards" (DWORD, the count), and a DWORD per card named by its
  number from 1.
- `...\3D Data\Sort Order`: a DWORD per card, named by its number.
- `...\3D Data\Card<n>`: "Name", "Module" and "Group" (strings), "Types"
  (DWORD, the number of PCI IDs), and `Type<m>` subkeys with "VendorID"
  and "DeviceID" (DWORDs).

If the key exists with a "Version" at least the program's own, it stops
and changes nothing; an older one is deleted first. The install's
program is version 14 with 18 cards, at most 3 PCI IDs each; the CD's is
version 22 with 21, at most 10 IDs each. The CD's adds the SiS 6326,
ATI Rage 128 and 3dfx Voodoo3 (vendor 0x121a, devices 4 and 5), and
more IDs for the Intel i740, ATI Rage Pro, 3Dlabs Permedia 2 and S3
Savage 3D. Both give the Voodoo2 and the Banshee the module `voodoo2`
and the Voodoo Graphics and Voodoo Rush `voodoo` (the Rush by three IDs:
3dfx's, Alliance's and Macronix's), the software renderer `softtri`,
most others `d3d`, the ATI Rage Pro `rage`, the Rendition `verite` and
the PowerVR `powervr`. That is why the readme asks for `3DDATA.EXE`
first: without its Voodoo3 entry in the registry, the new
`3ddata.dat`'s Voodoo3 line has no card to match. Run after it, the
install's older program leaves its newer table alone.

## For Reluva

Nothing to do. The game never sees these files; it sees only the
registry values 3D Device Setup leaves, which Reluva's host gives it
directly (the renderer named by `GameHost::Options::renderer` or
`presentedRenderer`, docs/engines/nfs/games/nfs3/PLAN.md). The one fact that carries
over is the mapping it confirms: the patch meant the Voodoo3 to run the
game through `voodoo2a.dll`, as Reluva's `-glidedll` runs the patch DLL
on its Voodoo3 board.
