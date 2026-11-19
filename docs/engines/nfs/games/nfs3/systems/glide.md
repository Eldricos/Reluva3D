# Need for Speed III: its renderers on the Glide stand-in

> **Status:** done · **Code:** `src/engines/nfs/host/game_host.cpp` (`addGlide`), the stand-in in `src/graphics/glide/` and `src/emulation/x86/glide/` · **Checked against:** the game's own frames, natively drawn and in recordings · **Confidence:** confirmed unless marked

How NFS III's own Voodoo renderer DLLs run on Reluva's Glide stand-in ([docs/common/graphics/glide.md](../../../../../common/graphics/glide.md)): `reluva-tool nfs host -glide`, the Play page's `renderer=glide`.

## NFS III's voodoo2a.dll

What it uses (RE, the 59 names it looks up and their call sites):

- Detection (`THRASH_is`, `0x60001bf0`): `grSstQueryBoards`,
  `grGlideInit`, `grSstQueryHardware`, `grGlideGetVersion`. It skips to the
  version string's first digit and wants it at least "2.42"; up to "2.5"
  it rates the card 100, above it 95 (strings at `0x6000f4ec`,
  `0x6000f4f4`). Reluva reports 2.46.
- Board (`0x600018c0`): the type 3 (`GR_SSTTYPE_Voodoo2`) goes into the
  structure THRASH_about reports at +0x6c; the frame buffer size decides
  how many buffers each of its 16 modes allows; the TMU0 memory range
  (`grTexMaxAddress - grTexMinAddress`) is the texture budget.
- Opening (`0x60001f80`): `grSstWinOpen(hwnd, GR_RESOLUTION_NONE, 0, ARGB,
  upper left, buffers, aux)`, then on failure the mode's own resolution
  from its table at `0x600100e8` (mode 1 is `GR_RESOLUTION_640x480`). After
  it: `grAlphaTestReferenceValue(0x10)`, `grTexCombine(0, LOCAL, ZERO,
  LOCAL, ZERO)`, `grHints(GR_HINT_ALLOW_MIPMAP_DITHER, 1)` and its state
  defaults through its own `setstate`.
- Vertices (`THRASH_drawtri`, `0x60003570`): x, y; `oow` the game's 1/w;
  `ooz` z × 65536; sow and tow u and v × 1/w × 256; r, g, b, a from the
  packed colour through a byte-to-float table. TMU0's own `oow` is left
  unset: with no stw hint Glide takes the vertex's.
- Textures: `THRASH_talloc` (`0x600022b0`) places each texture with
  `grTexCalcMemRequired` so it doesn't cross a 2 MB line; `tupdate`
  downloads with `grTexDownloadMipMap` and the palette with
  `grTexDownloadTable(0, GR_TEXTABLE_PALETTE)`; `settexture` calls
  `grTexSource` and sets the combine to texture × vertex colour
  (`grColorCombine(SCALE_OTHER, LOCAL, ITERATED or CONSTANT, TEXTURE)`).
- Frames: `clearwindow` is `grBufferClear`, `pageflip` waits for
  `grBufferNumPending` to reach 0 and calls `grBufferSwap`;
  `lockwindow`/`readrect`/`writerect` use the LFB functions.

`GameHost` (`Options::glide`) loads the install's `voodoo2a.dll` as the
renderer and answers `LoadLibraryA("glide2x.dll")` with the stand-in. The
frame shown is the front buffer, presented at each `grBufferSwap` and
after any THRASH call that drew or wrote into the front buffer
(`Glide2::takeFrontChanged`): the game writes its movies through
`lockwindow` straight into the front buffer, which a Voodoo shows at once.
The game is told its renderer is `voodoo2a.dll`, as with the native
THRASH, so it sends the same calls either way.

For comparisons, `reluva-tool nfs host -glidecompare out.csv` also draws every THRASH
call natively beside the DLL and compares the two frames of each flip,
and `-glidefilter point|bilinear` holds the texture filter whatever the
DLL asks (thrash.md, The Glide stand-in).

`Options::glideGpu` picks what draws: the GPU backend when there is a
device (the Play page's default with `renderer=glide`; nfs host
`-glidegpu`), else the software reference (`glide=software`), or both
compared (`-glidegpucompare out.csv`, a row per frame: the swap, the mean
difference, the pixels differing, those off by more than 24, the largest
difference). A device of its own each time (`gfx::createCompute`), on the
game's thread.

## NFS III's Voodoo3 patch DLL (Glide 3)

The Classic CD's `Patch/Voodoo3/voodoo2a.dll` (1999-04-21) loads
`glide3x.dll`. `Options::glideDll` (nfs host `-glidedll <file>`, the Play
page's `glidedll=`) loads such a file from outside the install in
`voodoo2a.dll`'s place; a file naming `glide3x.dll` gets `Glide3Dll` on
`voodoo3Board()` (or the board `-glideboard` names). It looks up 55 Glide
3 functions, all carried out. It identifies the board by
`grGetString(GR_HARDWARE)`, takes its buffer counts from
`grQueryResolutions` (for every Glide 3 board it takes the
`GR_MEMORY_UMA` path, whatever the value: thrash.md), opens 640x480 with two buffers and
a depth buffer, sets the layout of the game's own 32-byte vertex (window
coordinates, packed ARGB) and draws every triangle with `grDrawTriangle`.
It runs the menus, loading screens and races; what it asks for, and that
it draws the game without fog, is in
[thrash.md](thrash.md), The Voodoo3 patch's voodoo2a.dll.

## NFS III's voodooa.dll (Voodoo Graphics)

The install's `voodooa.dll` (the module 3D Device Setup gives a Voodoo
Graphics card; Watcom, as the retail `voodoo2a.dll`) runs unchanged on the
Glide 2 stand-in on a Voodoo Graphics board: `reluva-tool nfs host -glidedll
<install>/voodooa.dll -glideboard voodoo1` (the Play page:
`glidedll=` and `glideboard=voodoo1`). It looks up the same Glide 2
functions as `voodoo2a.dll`; what it does differently, from the code (RE,
`capstone`, both DLLs side by side) and the run:
- **Vertices**: x and y with 3 × 2^18 added and not taken off (Boards,
  above); z, 1/w and the colours as `voodoo2a.dll` does. Unread, on a
  board that takes them as they are, every triangle is far off the screen:
  the first run drew nothing but the clears.
- **The board** (`0x600018c0`): it reads `grSstQueryHardware`'s type,
  frame buffer size, Pixelfx revision and SLI. A Voodoo Rush (type 1)
  loses the modes wider than 800 and the SLI doubling; on an SST-1 (type
  0) +0x6c of `about()` becomes 2 or 3 if the revision has bit 8 or 12
  set, or 3 with no revision and 6 MB of frame buffer or more. The texture
  budget is `grTexMaxAddress − 2 × grTexMinAddress`, rounded up to 16, as
  in `voodoo2a.dll` and the Voodoo3 patch.
- **Alpha test**: `grAlphaTestFunction(GR_CMP_GEQUAL)` where
  `voodoo2a.dll` uses `GR_CMP_GREATER` (the counted modes: 6 against 4),
  both against 0x10.
- `grAADrawTriangle` and `grAADrawLine` are looked up but not called in
  the run.

The run (the clicks of thrash.md's comparisons, 5 billion instructions,
3,942 frames, Release build, software): the menus, Hometown's loading
screen and the race. On a 2 MB-texture board the game leaves the cockpit
out: its budget, `about()`'s +0x70, is under 2 MB, and the cockpit loader
(`nfs3.exe` `0x488a80`) wants more (thrash.md, The Voodoo Graphics
module). With `-glideboard voodoo1,tmu=4` it has the cockpit, and against
the retail `voodoo2a.dll` on the Voodoo2 the frames differ by a mean of
0.014-0.028 of 255 in the menus (about 1,000 pixels, along the edges of
the alpha-tested title letters: the alpha test), none in the loading
screen, and 0.13-0.37 in the race's first frames (2,400-3,000), growing
after as the two runs' game clocks part (0:09.03 against 0:08.98 at frame
2,600: the DLLs' own instructions count towards the deterministic clock).

## Sources and checks

- **Where the facts come from:** the DLLs' own calls to Glide, logged in runs of the hosted game, and their disassembly where the sections above give addresses.
- **How it was checked:** each DLL's frames against the native THRASH drawing's (`-glidecompare`) and the GPU's against the software stand-in's (`-glidegpucompare`), as the sections above say; the game-files test's `host_glide` step.
