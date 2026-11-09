# The Glide stand-in

Status: **Glide 2 working for NFS III's renderer.** `src/common/glide/`
is a clean-room stand-in for 3dfx's Glide library: a version-neutral
model of a Voodoo board, a Glide 2 front end over it, and a binding that
serves it to Reluva's x86 interpreter as `glide2x.dll`. Need for Speed
III's own `voodoo2a.dll` (1998-07-31) runs on it unchanged, front end and
races (`reluva-tool nfs-host -glide`, the Play page's `renderer=glide`).
Glide 3 (`glide3x.dll`) is not there yet; the core is built for it
(Extension points).

## Sources, and how each fact was found

The rules (CONTRIBUTING.md, and this work's own): 3dfx's public documents
and the interface definitions in its public SDK headers, used as
interface facts only; reverse engineering of the game's own DLL for how it
calls Glide. **No implementation source was used**: not 3dfx's released
Glide sources, nor dgVoodoo, nGlide, OpenGlide, MAME, 86Box, PCem or any
other wrapper or emulator.

- **Behaviour**: the *Glide 2.4 Reference Manual* and *Glide 2.4
  Programming Guide* (3dfx, July 1997), and the *Voodoo2 Graphics*
  specification (revision 1.16, December 1999), as kept on bitsavers
  (`components/3dfx/`). Where a function's effect is given there, the code
  follows it and says where.
- **Interface values**: function names and signatures, structure layouts
  and constant values from the headers of 3dfx's Glide 2.x SDK for Linux
  (`glide.h`, `sst1vid.h`, `glidesys.h`, `3dfx.h`, `glideutl.h`, as packaged
  in Slackware 7.1's `contrib/3dfx/sdk2x.tgz`; only the headers were
  extracted). `glide_defs.h` restates the values in Reluva's own names. No
  header is copied into the repository.
- **What NFS III needs**: `voodoo2a.dll` read with `pefile` and `capstone`
  (scripts kept outside the repository). It loads `glide2x.dll` by name
  and looks up 59 functions with GetProcAddress (`0x60001020`, the
  pointers at `0x60011414`-`0x600114fc`); the call sites give the
  arguments it passes. Facts found this way are marked (RE) below.

Where neither documents nor the DLL settle something, the code makes a
choice and says so; those are listed under Open questions.

## Architecture

| Layer | Files | Depends on |
|---|---|---|
| Interface values | `glide_defs.h` | nothing |
| Textures | `textures.h/.cpp`: formats, widening to ARGB, level sizes and placement | `glide_defs.h` |
| Pixel units | `pixel.h/.cpp`: combine units, fog, w float, blending, dither | `glide_defs.h` |
| Pipeline core | `pipeline.h/.cpp`: `Board`, `State`, `Vertex`, `Pipeline` (buffers, TMU memory, LFB, output), `Renderer` interface | the above, `common/types.h` |
| Software rasterizer | `software_renderer.cpp`: the reference `Renderer` | the core |
| Glide 2 front end | `glide2.h/.cpp`: `Glide2`, each Glide 2.x function on host values | the core |
| x86 binding | `glide2_x86.h/.cpp`: `Glide2Dll`, the stdcall exports for `common/x86` | the front end, `common/x86` |
| NFS III glue | `src/engines/nfs/host/thrash.cpp` (`GameHost::addGlide`), `play_page.cpp`, `nfs_commands.cpp` | all of it |

Nothing under `src/common/glide/` knows about NFS.

### The pipeline core

Version-neutral: levels are named by the log2 of their longer side (8 for
256), aspect ratios by log2(width / height) (3 for 8x1), so Glide 2's
`GR_LOD_*`/`GR_ASPECT_*` and Glide 3's log2 constants both convert to it.
The combine, blend, compare, depth, fog and texture format numbers are the
same in both APIs and are used as they are.

- `Board`: the generation (Voodoo Graphics, Rush, Voodoo2, Banshee,
  Voodoo3), TMU count (1-3), texture memory per TMU, frame buffer memory,
  revisions, SLI, the largest resolution. NFS III's board is a Voodoo2 with
  4 MB of frame buffer and two TMUs of 4 MB (the 12 MB card).
- Frame buffer: three 16-bit 5:6:5 colour buffers (front, back, triple)
  and the 16-bit auxiliary buffer (depth, or alpha), at the resolution
  `grSstWinOpen` asks for. `open()` checks the board's memory as
  Programming Guide table 3.3 does.
- Texture memory: each TMU's bytes, its 256-entry palette and two NCC
  tables. Downloads write the bytes; sampling reads and widens them, so a
  texture overwritten in memory changes as on the card.
- `State`: everything the Glide state functions set, per TMU where Glide
  has it per TMU.
- `Renderer`: what draws. `makeSoftwareRenderer()` is the reference; a GPU
  backend would implement the same interface (`triangle`, `line`, `point`,
  `clear`, `sync` before the buffers are read or written from outside,
  `texturesChanged`).
- Output: `present()` widens the front buffer to 8 bits a channel (top
  bits repeated) through the gamma table (Reference Manual,
  `grGammaCorrectionValue`). `outputFilter` can replace that step: the
  place for the Voodoo2/3 video filter later.

### The software rasterizer

In the order of the Voodoo2 specification's pipeline (its section 3 and
the `fbzMode`, `fogMode`, `alphaMode` figures):

1. **Set-up.** x and y snapped to 1/16 pixel (the hardware's 12.4 fixed
   point). Signed area for culling: positive for a triangle turning
   clockwise on the screen with the origin at the top left (Reference
   Manual, `grCullMode`, whose table this matches; confirmed by NFS III's
   culling, `voodoo2a.dll` passing THRASH's cull state straight to
   `grCullMode` (RE, `0x60002b48`), which drops counter-clockwise triangles
   for state 1 as `d3da.dll` does).
2. **Coverage.** A pixel is drawn when its centre is inside the three
   edges, or on a left edge or the edge along the lower y (Reference
   Manual, `grDrawTriangle`), within the clip window (min inclusive, max
   exclusive) and the screen. Edges are evaluated exactly in 1/16 pixel.
3. **Values.** Colour, alpha, z (`ooz`), 1/w (`oow`), and per TMU s/w,
   t/w and its 1/w are planes over the snapped vertices, evaluated at
   each pixel centre. Which `tmuvtx[]` entries are read follows
   `grHints(GR_HINT_STWHINT)` as the Reference Manual describes it.
   Colours are clamped to 0-255.
4. **Textures.** For each TMU in use, from the last down to TMU0: s and t
   divided by the TMU's 1/w (0-256 across the texture at every level,
   Programming Guide, Texel Coordinate Systems), the level of detail from
   the screen-space derivatives (log2 of the texels a pixel spans, plus
   `grTexLodBiasValue`), clamped to the mipmap's levels; the magnification
   filter when the LOD is below the largest level, else the minification
   one (Voodoo2 specification, `textureMode` and `tLOD`). Point sampling
   takes the texel under the sample, bilinear filtering the four round it
   weighted in 1/256ths. Clamp or wrap per axis. Texels widen as the
   specification's `textureMode` table says (fields repeated; palette and
   NCC lookups). Then that TMU's `grTexCombine` with what the one above
   gives.
5. **Chroma-key** on the colour combine's "other" colour (Reference Manual,
   `grChromakeyMode`), before the combine.
6. **Colour and alpha combine** (`grColorCombine`, `grAlphaCombine`): the
   functions and factors of the Reference Manual's tables; the arithmetic
   of the specification's figures: (other - local) times the factor plus
   one, shifted right 8, the low bits truncated, the sum clamped to 0-255,
   then inverted if asked. `grAlphaControlsITRGBLighting` as described.
7. **Alpha test** against the reference value.
8. **Depth.** z: the iterated `ooz` clamped to 16 bits. w: the
   specification's 4.12 float of 1/w (`floatDepth`: the exponent counts
   leading zeros of the fraction's top 16 bits, the next 12 bits are the
   mantissa, inverted), 0 for 1/w of 1 or more and 0xffff below 2^-16.
   `grDepthBiasLevel` added; the compare-to-bias modes compare the bias and
   write the unbiased depth (Reference Manual, `grDepthBufferMode`).
9. **Fog** after the combine, before blending: iterated alpha, the fog
   table, or the iterated z's top 8 bits; the table indexed by the w
   float's top 6 bits and moved towards the next entry by its next 8 (the
   `fogMode` figure: index `{exponent, mantissa[11:10]}`, fraction
   `mantissa[9:2]`). That puts entry i at Glide's w(i) =
   2^(3 + i/4) / (8 - i mod 4) exactly, as `guFogTableIndexToW` gives it.
   `GR_FOG_MULT2` and `GR_FOG_ADD2` as the Reference Manual's table.
10. **Blending** with the Reference Manual's factors (`GR_BLEND_SRC_COLOR`
    is the destination colour on the source side, `GR_BLEND_ALPHA_SATURATE`
    `GR_BLEND_PREFOG_COLOR` on the destination side, as `glide.h` aliases
    them), each product `(c * (f + 1)) >> 8` as in the combine units. The
    destination widens from 5:6:5 by repeating its top bits.
11. **Dither and write.** To 5:6:5 by the 4x4 or 2x2 ordered matrix
    (`grDitherMode`), or truncated; colour, alpha and depth masks.

Lines step a pixel along the longer axis at each pixel centre crossed;
points are the pixel under them. Both go through the same pixel pipeline.

### The Glide 2 front end

`Glide2` holds a `Pipeline` and the application's colour format, and
converts Glide 2's constants. Implemented, with their documented meaning:

- Start-up and the board: `grGlideInit`, `grGlideShutdown`,
  `grGlideGetVersion` ("Glide Version 2.46"), `grSstQueryBoards`,
  `grSstQueryHardware`, `grSstSelect`, `grSstWinOpen` (fails for
  `GR_RESOLUTION_NONE`, as the Reference Manual says for SST-1 boards),
  `grSstWinClose`, `grSstControl`/`grSstControlMode`,
  `grSstScreenWidth/Height`, `grSstStatus` (idle, FIFOs empty),
  `grSstIdle`, `grSstIsBusy`, `grSstVRetraceOn` (alternates, so retrace
  waits end), `grSstVideoLine`, `grSstOrigin`, `grSstPerfStats`,
  `grSstResetPerfStats`, `grTriStats`, `grResetTriStats`, `grHints`,
  `grErrorSetCallback` (never called, as in the release build),
  `grGlideGetState`/`grGlideSetState` (the state kept host-side under a
  number written into the caller's `GrState`).
- Drawing: `grDrawTriangle`, `grDrawLine`, `grDrawPoint`, the polygon
  functions (fans), `guDrawTriangleWithClip` and friends, `grBufferClear`,
  `grBufferSwap`, `grBufferNumPending` (0), `grRenderBuffer`.
- State: every combine, blend, test, depth, fog, chroma-key, clip, mask,
  dither, cull and constant colour function; `grGammaCorrectionValue`;
  `guColorCombineFunction`, `guAlphaSource`, `grTexCombineFunction` and
  `guTexCombineFunction` as `grColorCombine`/`grAlphaCombine`/
  `grTexCombine` settings that give the effects their Reference Manual
  pages describe (Reluva's reading).
- Fog tables: `guFogTableIndexToW`, `guFogGenerateExp`, `...Exp2`,
  `...Linear`, normalized as their pages say.
- Textures: `grTexMinAddress` (0), `grTexMaxAddress` (the last 8-byte unit),
  `grTexCalcMemRequired`, `grTexTextureMemRequired`, `grTexSource`,
  `grTexDownloadMipMap`, `...Level`, `...LevelPartial`,
  `grTexDownloadTable`, `...Partial` (palette and NCC), `grTexNCCTable`,
  `grTexClampMode`, `grTexCombine`, `grTexDetailControl`,
  `grTexFilterMode`, `grTexLodBiasValue` (to the nearest quarter),
  `grTexMipMapMode` (with `GR_MIPMAP_NEAREST_DITHER`), `grTexMultibase`,
  `grTexMultibaseAddress`.
- The linear frame buffer: `grLfbLock` (front, back and auxiliary
  buffers, every write mode, either origin, through the pixel pipeline or
  not), `grLfbUnlock`, `grLfbConstantAlpha`, `grLfbConstantDepth`,
  `grLfbWriteRegion` (every source format; RLE16 read as runs across rows),
  `grLfbReadRegion` (5:6:5).

Texture memory layout: the specification's table (section 10, Texture
Memory Access) gives each level's size, 16-bit levels in 8-byte units with
their sides counted as at least 2 texels and 8-bit levels half that; a
mipmap's levels follow each other from its largest, the even or odd ones
only when split (`evenOdd`). `grTexDownloadMipMap` reads every level from
its data, largest first, rows packed, and keeps those `evenOdd` names.

Stubs (log once, return 0): the utility library's texture manager
(`guTexAllocateMemory`, `guTexChangeAttributes`, `guTexDownloadMipMap`,
`...Level`, `guTexGetCurrentMipMap`, `guTexGetMipMapInfo`,
`guTexMemQueryAvail`, `guTexMemReset`, `guTexSource`,
`guTexCreateColorMipMap`), `gu3dfGetInfo`, `gu3dfLoad`, `guEncodeRLE16`,
`guEndian*`, `grSplash`, `ConvertAndDownloadRle`. Carried out only in part,
logged once: the anti-aliased primitives (drawn without coverage alpha),
`grLfbWriteColorFormat` and `grLfbWriteColorSwizzle` (the window's own
format only).

### The x86 binding

`Glide2Dll` makes one native function per export, named as stdcall
decorates it (`_grDrawTriangle@12`), popping its arguments. It reads the
structures the arguments point at from guest memory:

- `GrVertex`: 60 bytes, x, y, z, r, g, b, ooz, a, oow, then two TMUs'
  sow, tow, oow (`glide.h` with `GLIDE_NUM_TMU` 2; `voodoo2a.dll` builds
  its vertices 0x3c apart on the stack (RE, `0x60003570`)).
- `GrTexInfo`: smallLod, largeLod, aspectRatio, format, data.
- `GrHwConfiguration`: num_sst, then per board the type and the
  `GrVoodooConfig_t` (fbRam, fbiRev, nTexelfx, sliDetect, tmuConfig[2]),
  36 bytes a board (`voodoo2a.dll` reads the type at +4, fbRam at +8,
  fbiRev at +0xc and sliDetect at +0x14 (RE, `0x600018c0`)).
- `GrLfbInfo_t`: size (must be at least 20), lfbPtr, strideInBytes,
  writeMode, origin.

**Linear frame buffer locks.** The pipeline's buffers are host memory, so
a lock gives the guest a copy: a block of guest memory per buffer, write
mode and origin, 1024 pixels a row as the Voodoo's logical scan line
(2048 or 4096 bytes; Voodoo2 specification section 9), filled from the
buffer on the first lock. On the last unlock the pixels that changed are
written back, as the lock's write mode says (and through the pixel
pipeline if it asked). Read and write locks of one buffer share the copy,
as NFS III takes both at once (`THRASH_lockwindow`, `0x60002ef0`: a write
lock, then a read lock of the same buffer, keeping the write pointer).

A host uses `exports()` to answer `LoadLibraryA("glide2x.dll")` and
`GetProcAddress`, or `provideTo()` a `Win32Process` for programs that
import it directly.

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

For comparisons, `nfs-host -glidecompare out.csv` also draws every THRASH
call natively beside the DLL and compares the two frames of each flip,
and `-glidefilter point|bilinear` holds the texture filter whatever the
DLL asks (thrash.md, The Glide stand-in).

## Tests

`tests/test_glide.cpp` (the `glide` suite): the dither (truncation, both
matrices averaging to the level), every texture format's widening and
the NCC and palette lookups, the specification's level sizes and its
worked memory example, the combine units, the w float and the fog table's
entries at Glide's w(i), the fog modes, blending, the fill rule (shared
edges drawn once, a 4x4 square exactly 16 pixels), culling, the clip
window, z and w buffering and compare-to-bias, chroma-key, alpha test,
table fog through the rasterizer, textures through the Glide 2 front end
(point and bilinear, clamp and wrap, a palette texture), the linear
frame buffer (write modes, origins, the auxiliary buffer, regions, the
pixel pipeline, front-buffer changes, swapping, presenting), and the x86
binding (every name `voodoo2a.dll` looks up exported, calls through the
interpreter, a lock's guest copy written back, a stub counted).

## Compared with the native THRASH and softtria

See [thrash.md](../formats/nfs/thrash.md), The Glide stand-in, for runs,
numbers and the causes of the differences.

## Extension points

- **Glide 3** (`glide3x.dll`, the Classic CD's Voodoo3 patch DLL): a
  `Glide3` front end over the same `Pipeline`: `grVertexLayout` and
  `grDrawVertexArray*` read vertices by the layout given (the patch DLL
  uses window coordinates and packed ARGB, thrash.md), `grGet`/
  `grGetString`, `grEnable`/`grDisable`, `grCoordinateSpace`, Glide 3's
  log2 LOD and aspect constants, `grTexDownloadTable` without a TMU,
  `grLfbWriteRegion` with its pixel-pipeline flag, `guGammaCorrectionRGB`
  (three gamma values: `State::gamma` becomes per channel). A
  `Glide3Dll` binding next to `Glide2Dll`.
- **Boards**: `Board::generation` and the sizes. Voodoo Graphics and Rush
  differ in memory, TMU revision (no palette on revision 0), the Rush's
  windowed `GR_RESOLUTION_NONE`; Voodoo3/Banshee in resolutions, 32 MB
  textures, a larger fog table (the patch DLL resamples its table to
  `grGet(GR_FOG_TABLE_ENTRIES)`).
- **TMUs**: up to three; the rasterizer already chains them
  (`grTexCombine` with other), and NFS III's patch DLL would load every
  texture into TMU1 too.
- **A GPU backend**: another `Renderer`. The pixel units in `pixel.h` are
  the reference its shaders would be checked against; `sync()` is where
  it copies its target back before an LFB lock or a presented frame.
- **Register level**: the state maps to `fbzMode`, `fbzColorPath`,
  `alphaMode`, `fogMode`, `textureMode` and `tLOD` bit for bit; a register
  front end could feed the same `State`.
- **Video output**: `Pipeline::outputFilter` for the Voodoo2/3 video
  filter and the hardware gamma table.

## Open questions

These are choices in the code that the documents leave open:

- The colour dither's matrix values and rounding (the documents name 4x4
  and 2x2 ordered matrices only); Reluva uses the Bayer matrices with
  thresholds (2k + 1) / 2n of a step, as the native THRASH does
  (thrash.md, Dithering). Dither subtraction before blending (`fbzMode`
  bit 19) is not done.
- The blend's rounding: the combine units' `(c × (f + 1)) >> 8` is used.
- The LOD's exact arithmetic (the hardware's own from the gradients of
  s/w, t/w and 1/w) and the LOD dither's pattern.
- Bilinear weights: 1/256ths here; the hardware's fraction width isn't
  given.
- Texture memory: the specification's table and its own worked examples
  disagree for 8x1 and 4x1 levels (the table counts them as 4 and 2
  units, the examples 2 and 1); Reluva follows the table. It matters only
  for where a mipmap's small levels lie and what `grTexCalcMemRequired`
  returns.
- Defaults the documents contradict or don't give: the constant colour
  (0, as `grSstWinOpen` says; `grConstantColorValue`'s page says
  0xFFFFFFFF), the depth mask (off, `grDepthMask`'s page), each TMU's
  combine (decal), filter (point-sampled) and clamp (wrap).
- When the auxiliary buffer holds alpha: here when depth buffering is off
  and blending reads the destination alpha.
- `grTexMaxAddress`'s exact value (here the memory size less 8).
