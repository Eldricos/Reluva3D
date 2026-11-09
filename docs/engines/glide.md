# The Glide stand-in

Status: **Glide 2 and Glide 3 working for NFS III's renderers, in software
and on the GPU.** `src/common/glide/` is a clean-room stand-in for 3dfx's
Glide library: a version-neutral model of a Voodoo board, Glide 2 and
Glide 3 front ends over it, bindings that serve them to Reluva's x86
interpreter as `glide2x.dll` and `glide3x.dll`, a software reference
rasterizer and a GPU backend that follows it. Need for Speed III's own
`voodoo2a.dll` (1998-07-31, Glide 2) runs on it unchanged, front end and
races (`reluva-tool nfs-host -glide`, the Play page's `renderer=glide`),
and so does the Classic CD's Voodoo3 patch DLL (1999-04-21, Glide 3;
`nfs-host -glidedll`, the Play page's `glidedll=`).

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
- **Glide 3's behaviour**: the *Glide 3.0 Reference Manual* and *Glide 3.0
  Programming Guide* (3dfx, June 1998, bitsavers `components/3dfx/`), and
  the *Voodoo3* specification (revision 1.0, November 1999) for the
  board's fog table (64 entries, its `fogTable` register).
- **Interface values**: function names and signatures, structure layouts
  and constant values from the headers of 3dfx's Glide 2.x SDK for Linux
  (`glide.h`, `sst1vid.h`, `glidesys.h`, `3dfx.h`, `glideutl.h`, as packaged
  in Slackware 7.1's `contrib/3dfx/sdk2x.tgz`; only the headers were
  extracted), and for Glide 3 the same files of its Glide 3.x SDK
  (`contrib/3dfx/sdk3x.tgz`, headers only: `GrTexInfo` with log2 LODs,
  `GrResolution`, `GrLfbInfo_t`, the `GR_PARAM_*`, `grGet` and
  `grGetString` numbers, the renumbered fog modes and texture bases).
  `glide_defs.h` restates the values in Reluva's own names (Glide 3's in
  namespace `g3`). No header is copied into the repository.
- **What NFS III needs**: both `voodoo2a.dll` versions read with `pefile`
  and `capstone` (scripts kept outside the repository). It loads `glide2x.dll` by name
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
| Set-up | `raster_setup.h/.cpp`: snapping, culling, the fill rule's spans, the planes, lines' and points' pixels | the core |
| Software rasterizer | `software_renderer.cpp`: the reference `Renderer` | the set-up |
| GPU backend | `gpu_renderer.h/.cpp`, `shaders/rasterize.comp` (built into `shaders/glide_kernels.h` by `shaders/build.sh`): a `Renderer` on `gfx::Compute`, and the comparing renderer | the set-up, `common/gpu_compute.h` |
| Glide 2 front end | `glide2.h/.cpp`: `Glide2`, each Glide 2.x function on host values | the core |
| Glide 3 front end | `glide3.h/.cpp`: `Glide3`, what Glide 3 changed; the rest through a `Glide2` | the Glide 2 front end |
| x86 bindings | `glide_x86.h/.cpp` (`GlideDll`: exports, stubs, linear frame buffer copies), `glide2_x86.h/.cpp` (`Glide2Dll`), `glide3_x86.h/.cpp` (`Glide3Dll`) | the front ends, `common/x86` |
| NFS III glue | `src/engines/nfs/host/thrash.cpp` (`GameHost::addGlide`), `play_page.cpp`, `nfs_commands.cpp` | all of it |

`gfx::Compute` (`src/common/gpu_compute.h`) is general GPU work: storage
buffers, compute kernels, uploads and read-backs, in order. The SDL
backend gives it on a GPU device of its own (`backends/sdl3/sdl_compute.cpp`,
SDL's GPU API, Vulkan or Metal), which needs no window, so `reluva-tool`
links it too; each program registers it at start-up
(`gfx::setComputeFactory`). `gfx::Renderer3D` was not used: it draws
fixed-function, into a window's frame, and can't run the Voodoo's pixel
units, read 5:6:5 back for blending or compare 16-bit depths as the board
does.

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
- `Renderer`: what draws. `makeSoftwareRenderer()` is the reference,
  `makeGpuRenderer()` the GPU backend. The interface: `triangle`, `line`,
  `point`, `clear`; `sync` before the buffers are read or written from
  outside, `syncBuffer` when only one is (a presented frame);
  `textureMemoryChanged` with the bytes, `tablesChanged`, `swapped`. A
  pipeline's colour buffers sit in fixed slots (`slotOf`, `slotPixels`):
  a swap changes which slot is the front, so a renderer keeping copies
  keeps them by slot.
- Output: `present()` widens the front buffer to 8 bits a channel (top
  bits repeated) through the gamma table, one a channel (Glide 2's
  `grGammaCorrectionValue` sets all three, Glide 3's `guGammaCorrectionRGB`
  each). `outputFilter` can replace that step: the
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

### The GPU backend

`makeGpuRenderer(gfx::Compute)`: the same pixels by the same rules, the
per-pixel work on the GPU (`gpu_renderer.cpp`, `shaders/rasterize.comp`).

- **Set-up on the CPU, shared.** Each triangle goes through the software
  rasterizer's own set-up (`raster_setup.h`): snapped vertices, culling,
  the fill rule's span of pixels on each row (exact, in 1/16 pixel), the
  plane of each value. Lines and points become their pixels, each with
  its values. So coverage is identical by construction.
- **Batches and tiles.** Primitives collect into a batch, each with its
  rows' spans and an index into a list of state records (everything the
  pixel units read: combine settings, constant colour, chroma-key, alpha
  test, depth, fog table and colour, blending, dither, masks, the target
  slot; per TMU its format, filters, clamps, LOD range and bias, mipmap
  mode, combine, detail, each level's address and size), a new record
  only when the state changes. A batch is binned into 16x16 tiles in
  drawing order and sent as one buffer; the kernel runs one workgroup a
  tile that has primitives, one thread a pixel, going through the tile's
  primitives in order. So each pixel sees the writes of those drawn before
  it, as on the board, whatever the GPU's own blending or depth units
  would do.
- **The pixel pipeline in the kernel**, ported from `pixel.h`,
  `textures.cpp` and `software_renderer.cpp` with integer arithmetic:
  texel fetch from raw TMU memory (every format, palette and NCC lookups),
  point and bilinear filtering in 1/256ths, clamp and wrap, the LOD from
  the planes' gradients with bias, nearest and dithered mipmapping, the
  TMUs chained through their combine units (detail and LOD fraction
  factors), chroma-key on the colour combine's other input, the colour and
  alpha combine units ((other - local) × (factor + 1) >> 8), alpha test,
  z and w buffering (the 4.12 float of 1/w, the bias and compare-to-bias
  modes), fog (iterated alpha, iterated z, the table read at the w
  float's index and fraction, MULT2/ADD2), blending against the 5:6:5
  destination widened by repeating its top bits (and the auxiliary buffer's
  alpha), the 4x4 and 2x2 ordered dither to 5:6:5, the colour, alpha and
  depth masks. Clears are primitives too, so they keep their place.
- **Memory on the GPU.** The frame buffer is 16 bits a pixel as on the
  board (one 32-bit word each in a storage buffer: three colour slots and
  the auxiliary buffer), texture memory a byte-addressed copy of every
  TMU's. Palettes and NCC tables travel with the batch, a copy each time
  they change, so a batch can hold draws with different tables. A texture
  download sends the batch first, then the changed bytes.
- **The linear frame buffer.** `sync()` sends the batch and reads back
  every buffer the GPU drew into since; `syncBuffer()` (a presented frame)
  only that one. What the host then writes into the pipeline's buffers
  (lock write-backs, `grLfbWriteRegion`) is found by comparing them with
  what was read back, row by row, and sent to the GPU before the next
  primitive; a direct write first syncs, so it lands on what was drawn.
  Writes through the pixel pipeline are points, drawn on the GPU.

Deviations from the software reference:
- **The planes per pixel.** The kernel evaluates each value's plane at
  each pixel centre in 32-bit floats; the software rasterizer evaluates it
  at the start of each row's span and then adds the x gradient pixel by
  pixel. The two round differently. A value right at a texel boundary, a
  depth's last bit or a w float's mantissa can come out one step apart,
  so a few texels, depths and fog factors differ (Comparisons, below).
- **log2** of the texel footprint is the GPU's `log2`, not the C
  library's: an LOD right at a level boundary can pick the other level.
- **Statistics.** The GPU doesn't count pixels: `grSstPerfStats`' and
  `grGet(GR_STATS_PIXELS_*)`'s pixel counters count only clears there;
  triangle counts are kept (on the CPU).
- **Order within a pixel** is kept; between pixels the GPU is free, as
  the board's own pixel order doesn't show in the result.

**Comparing.** `makeComparingRenderer(tested, onSwap)` draws every call
with the tested renderer into the pipeline and with the software
reference into a pipeline of its own (the same state, texture memory,
tables and what the host wrote copied over), and at each buffer swap
reads the tested frame back and compares: the mean difference per
channel, of 255, the pixels differing and those off by more than 24.
`nfs-host -glidegpucompare out.csv` writes it per frame.

On NFS III, through the menus, a loading screen and a race (3,909 frames),
the GPU's frames differ from the software reference's by 0.0013 of 255 on
average, about 170 pixels a frame, at most 3 by more than 24
([thrash.md](../formats/nfs/thrash.md), The Glide stand-in, On the GPU).
Speed on this machine says little: its Vulkan is llvmpipe on the same
CPU, and the interpreter is the limit (the same numbers there).

`tests/test_glide_gpu.cpp` (the `glide_gpu` suite) draws a scene of every
feature both ways: of 307,200 pixels 128 differ in the first frame (single
texels at boundaries in the high-frequency checker textures, up to a whole
texel's colour), 7 in the second (a smooth gradient, a dither step), and
1,298 auxiliary buffer values (w-buffer depths one step apart). Without
a GPU device the suite says so and passes.

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

### The Glide 3 front end

`Glide3` (`glide3.h`) holds a `Glide2` over the same pipeline (`core()`)
for every function whose meaning and values Glide 3 kept (the combine,
blend, test, depth, chroma-key, clip, mask, dither, cull and constant
colour functions, buffer clears and swaps, texture filters, clamps, LOD
bias, combine, detail, the linear frame buffer locks), and carries out
what changed, from the Glide 3.0 Reference Manual:

- **Vertices.** `grVertexLayout(param, offset, mode)` says where each
  parameter is from the vertex's pointer: `GR_PARAM_XY`, `Z`, `W`, `Q`,
  `A`, `RGB` (three floats) or `PARGB` (packed ARGB bytes), `STn`, `Qn`.
  `grCoordinateSpace(GR_WINDOW_COORDS)`: x and y in pixels, z as Glide 2's
  `ooz`, q as `oow`, s and t already s/w and t/w (0-256 a repeat), each
  TMU's own q if given else the vertex's. `GR_CLIP_COORDS`: divided by w
  and put through `grViewport` and `grDepthRange` as their pages give it
  (x = (x/w + 1) × width / 2 + x0), colours 0-1, s and t 0-1 a repeat.
  Parameters a layout leaves out keep their last values (the manual: a
  disabled parameter "may inherit the last known value"); a TMU without
  its own s and t takes TMU0's (Glide 2's default; Reluva's reading).
- **Drawing.** `grDrawTriangle`, `grDrawLine`, `grDrawPoint` take vertex
  pointers; `grDrawVertexArray` and `grDrawVertexArrayContiguous` take
  points, line strips, lines, polygons (fans), strips, fans, triangles,
  and the `_CONTINUE` forms (a strip continued keeps its last two
  vertices and its parity, a fan its first and last).
- **Queries.** `grGet` answers the board's numbers (bits, fog and gamma
  table sizes, the state and layout buffer sizes, memory, boards, TMUs,
  revisions, statistics, the viewport, depth ranges), writing only if the
  caller's buffer is large enough and returning the bytes written;
  `grGetString` the extension list (empty), hardware ("Voodoo3 (tm)",
  "Voodoo2"..., the names NFS III's patch DLL compares, RE `0x600115f0`),
  renderer, vendor and version ("3.10", the version the patch DLL takes
  as current, `0x600115e8`); `grQueryResolutions` every resolution,
  buffer count and auxiliary buffer that fits the board's memory, for
  the template's fields (`GR_QUERY_ANY` for any).
- **Modes.** `grEnable`/`grDisable`: `GR_ALLOW_MIPMAP_DITHER` (until it,
  `GR_MIPMAP_NEAREST_DITHER` is plain nearest, as `grTexMipMapMode`'s page
  says), the rest kept and logged once where they'd change the picture
  (`GR_AA_ORDERED`, `GR_VIDEO_SMOOTHING`).
- **Contexts.** `grSstWinOpen` returns a context (nonzero),
  `grSstWinClose` and `grSelectContext` take it.
- **Textures.** `GrTexInfo` and every LOD and aspect argument in log2
  form, converted for the core; `grTexDownloadTable(type, data)` and its
  partial form load the one palette or NCC table into every TMU ("Glide
  3.0 implements one global palette and two global NCC tables");
  `grTexNCCTable(table)` likewise; `grTexMultibaseAddress`'s ranges
  renumbered (`GR_TEXBASE_256` is 3).
- **Fog.** `grFogMode`'s numbers changed: `GR_FOG_WITH_TABLE_ON_Q` (2) is
  the core's table fog, `GR_FOG_WITH_ITERATED_Z` (3) as before,
  `GR_FOG_WITH_ITERATED_ALPHA_EXT` (4) the iterated alpha;
  `GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT` isn't offered (logged, the table on
  q). `grFogTable` reads `grGet(GR_FOG_TABLE_ENTRIES)` entries, the
  board's; the core keeps 64 and a larger table would be resampled (entry
  i from entry i × n / 64). The Voodoo2's and Voodoo3's have 64.
- **Gamma.** `guGammaCorrectionRGB` sets each channel's
  (c = 255 (c / 255)^(1/γ)).
- **The linear frame buffer.** `grLfbWriteRegion` takes Glide 3's pixel
  pipeline flag before the stride and writes through the pipeline if set.
- **State.** `grGlideGetState`/`SetState` save the core's state and the
  layout and enables under a number kept in the caller's buffer;
  `grGlideGetVertexLayout`/`SetVertexLayout` the layout. `grGet` reports
  64 bytes for each.

`voodoo3Board()`: a Voodoo3 with 16 MB, which Reluva splits 8 MB of frame
buffer and 8 MB for each of two TMUs (non-UMA, so `grGet(GR_MEMORY_UMA)`
is 0); revisions 1; up to 2048x1536. The split, the revisions and the
gamma table's 32 entries are Reluva's choices: the documents don't give
them.

Stubs (log once, return 0): `grLoadGammaTable`, `grSplash`,
`gu3dfGetInfo`, `gu3dfLoad`; `grGetProcAddress` returns 0 (no
extensions); the palette 6666 and mirror-clamp extensions aren't offered
(logged if asked).

### The x86 bindings

`GlideDll` (`glide_x86.h`) is what both share: the export table, stubs,
the guest copies for locks, the fog utilities, and counts of the modes
the mode-setting functions were asked for (`modes()`, which nfs-host
prints). `Glide2Dll` makes one native function per export, named as stdcall
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

`Glide3Dll` serves `glide3x.dll`: every function of the Glide 3.x SDK's
`glide.h` and `glideutl.h` under its stdcall name (`_grLfbWriteRegion@36`,
`_grTexDownloadTable@8`, `_grSstWinClose@4`...). It reads a vertex's
bytes from guest memory over the layout's extent (`layoutMin()` to
`layoutMax()` from its pointer), `GrTexInfo` with log2 fields,
`GrResolution` (resolution, refresh, colour and auxiliary buffers), and
puts `grGetString`'s strings in guest memory once each.

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

`Options::glideGpu` picks what draws: the GPU backend when there is a
device (the Play page's default with `renderer=glide`; nfs-host
`-glidegpu`), else the software reference (`glide=software`), or both
compared (`-glidegpucompare out.csv`, a row per frame: the swap, the mean
difference, the pixels differing, those off by more than 24, the largest
difference). A device of its own each time (`gfx::createCompute`), on the
game's thread.

## NFS III's Voodoo3 patch DLL (Glide 3)

The Classic CD's `Patch/Voodoo3/voodoo2a.dll` (1999-04-21) loads
`glide3x.dll`. `Options::glideDll` (nfs-host `-glidedll <file>`, the Play
page's `glidedll=`) loads such a file from outside the install in
`voodoo2a.dll`'s place; a file naming `glide3x.dll` gets `Glide3Dll` on
`voodoo3Board()`. It looks up 55 Glide 3 functions, all carried out
(`grAADrawTriangle` without its edge anti-aliasing). It identifies the
board by `grGetString(GR_HARDWARE)`, takes its memory and buffer counts
from `grGet` and `grQueryResolutions`, opens 640x480 with two buffers and
a depth buffer, sets the layout of the game's own 32-byte vertex (window
coordinates, packed ARGB) and draws every triangle with `grDrawTriangle`.
It runs the menus, loading screens and races; what it asks for, and that
it draws the game without fog, is in
[thrash.md](../formats/nfs/thrash.md), The Voodoo3 patch's voodoo2a.dll.

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
interpreter, a lock's guest copy written back, a stub counted). Glide 3:
`grGet` (and a buffer too small), `grGetString`, `grQueryResolutions`,
contexts, the patch DLL's vertex layout drawing a triangle from packed
ARGB, a strip's two triangles keeping their winding, log2 texture sizes,
the one palette in every TMU, the renumbered fog modes, mipmap dither
only once allowed, gamma per channel, clip coordinates through the
viewport; its binding (every name the patch DLL looks up exported,
`grGet` and `grGetString` through the interpreter, a triangle from guest
vertices by the layout, `grLfbWriteRegion@36`, the counted modes).

`tests/test_glide_gpu.cpp` (the `glide_gpu` suite): the GPU backend
against the software reference (The GPU backend, above).

## Compared with the native THRASH and softtria

See [thrash.md](../formats/nfs/thrash.md), The Glide stand-in, for runs,
numbers and the causes of the differences.

## Extension points

- **Glide 3's extensions** (`GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT`, the
  6666 palette, mirrored clamping, `GR_TEXFMT_ARGB_CMP_FXT1`...) and
  anti-aliasing (`GR_AA_ORDERED`, `grAADrawTriangle`'s edge coverage):
  not offered or drawn without; each logged once.
- **Boards**: `Board::generation` and the sizes. Voodoo Graphics and Rush
  differ in memory, TMU revision (no palette on revision 0), the Rush's
  windowed `GR_RESOLUTION_NONE`; Banshee and Voodoo3 in their memory
  split (UMA or not) and video. `Board::fogTableEntries` is where a board
  with a larger fog table would go (Glide 3's front end resamples to the
  core's 64).
- **TMUs**: up to three; the rasterizer already chains them
  (`grTexCombine` with other). NFS III's patch DLL would load every
  texture into TMU1 too, but only when `VOODOO2_NUMTMU` says 2 or more
  (thrash.md).
- **The GPU backend** could take the planes' arithmetic closer to the
  software one (each row's start, then stepped), at the cost of a serial
  loop per row; and count pixels for the statistics with atomics.
- **Clip coordinates**: no clipping against the view volume yet.
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
- Glide 3: a disabled parameter keeps its last value; a TMU without its
  own s and t takes TMU0's; `GR_TEXTURE_ALIGN` 8; 64 bytes for the state
  and layout buffers; "3.10" as the version string; the Voodoo3's memory
  split (8 MB frame buffer, 8 MB textures, non-UMA) and gamma table size;
  one refresh rate per resolution in `grQueryResolutions`.
