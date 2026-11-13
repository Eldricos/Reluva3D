# The Glide stand-in

Status: **Glide 2 and Glide 3 working for NFS III's renderers, in software
and on the GPU.** `src/graphics/glide/` is a clean-room stand-in for 3dfx's
Glide library: a version-neutral model of a Voodoo board, Glide 2 and
Glide 3 front ends over it, bindings that serve them to Reluva's x86
interpreter as `glide2x.dll` and `glide3x.dll`, a software reference
rasterizer and a GPU backend that follows it. Need for Speed III's own
`voodoo2a.dll` (1998-07-31, Glide 2) runs on it unchanged, front end and
races (`reluva-tool nfs-host -glide`, the Play page's `renderer=glide`),
and so does the Classic CD's Voodoo3 patch DLL (1999-04-21, Glide 3;
`nfs-host -glidedll`, the Play page's `glidedll=`). So does the install's
Voodoo Graphics module `voodooa.dll` on a Voodoo Graphics stand-in
(`-glideboard voodoo1`). Anti-aliased primitives, every linear frame
buffer write format with the colour formats and swizzles, the utility
library (texture manager, `.3df` files, RLE16, endian swaps), five board
generations as their specifications give them (the Voodoo Rush's memory
shared with the 2D desktop, the Banshee's own configuration), Glide 3's
extensions where a board has them (chroma range, mirrored textures, the
6666 palette) and Glide 3's clipping of clip coordinates, anti-aliased
edges included, are carried out too.

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
- **The other boards**: the *SST-1 Graphics Engine* specification
  (Voodoo Graphics, revision 1.61, December 1999), the *SST-96*
  specification (Voodoo Rush, revision 2.2, November 1999) and the
  *Banshee High Performance Graphics Engine* specification (revision 1.1,
  November 1998), bitsavers `components/3dfx/`
  (`Voodoo1_SST-1_Spec_r1.61_199912.pdf`,
  `Voodoo1_Rush_SST-96_Spec_r2.2_199911.pdf`,
  `Voodoo2_Banshee_Spec_r1.1_199811.pdf`); the Glide 2.4 manuals
  (`Glide_Reference_Manual_2.4_199707.pdf`,
  `Glide_Programming_Guide_2.4_199707.pdf`) for what Glide does on the
  SST-96. Read again in November 2026 for the boards (Boards, below).
- **`.3df` files**: the TexUS guide (*Texus V2.01, 3Dfx Interactive
  Texture Utility Software*, December 1996, section 4, "3df File
  Format"), a 3dfx user document (the copy at
  users.cg.tuwien.ac.at/wimmer/view3dx/TEXUS.html). Not TexUS's code.
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

- **NFS III's other DLLs**: the install's `voodooa.dll` (Voodoo Graphics,
  Glide 2) and the CD's Voodoo3 patch read the same way, and `nfs3.exe`
  with Reluva's `tools/nfsre` (its `asm` and `xref`) for what the game does
  with `about()`'s fields.

Where neither documents nor the DLL settle something, the code makes a
choice and says so; those are listed under Open questions.

## Architecture

| Layer | Files | Depends on |
|---|---|---|
| Interface values | `glide_defs.h` | nothing |
| Textures | `textures.h/.cpp`: formats, widening to ARGB, level sizes and placement | `glide_defs.h` |
| Pixel units | `pixel.h/.cpp`: combine units, fog, w float, blending, dither | `glide_defs.h` |
| Pipeline core | `pipeline.h/.cpp`: `Board`, `State`, `Vertex`, `Pipeline` (buffers, TMU memory, LFB, output), `Renderer` interface | the above, `common/types.h` |
| Set-up | `raster_setup.h/.cpp`: snapping, culling, the fill rule's spans, the planes, lines' and points' pixels; `antialias.cpp`: the anti-aliased primitives' coverage pixels | the core |
| Software rasterizer | `software_renderer.cpp`: the reference `Renderer` | the set-up |
| GPU backend | `gpu_renderer.h/.cpp`, `shaders/rasterize.comp` (built into `shaders/glide_kernels.h` by `shaders/build.sh`): a `Renderer` on `gfx::Compute`, and the comparing renderer | the set-up, `graphics/gpu_compute.h` |
| Glide 2 front end | `glide2.h/.cpp`: `Glide2`, each Glide 2.x function on host values, the texture manager | the core |
| Utility library | `glide_utils.h/.cpp`: `.3df` files, RLE16, endian swaps | the texture tables |
| Glide 3 front end | `glide3.h/.cpp`: `Glide3`, what Glide 3 changed; the rest through a `Glide2` | the Glide 2 front end |
| x86 bindings | `glide_x86.h/.cpp` (`GlideDll`: exports, stubs, linear frame buffer copies), `glide2_x86.h/.cpp` (`Glide2Dll`), `glide3_x86.h/.cpp` (`Glide3Dll`) | the front ends, `common/x86` |
| NFS III glue | `src/engines/nfs/host/thrash.cpp` (`GameHost::addGlide`), `play_page.cpp`, `nfs_commands.cpp` | all of it |

`gfx::Compute` (`src/graphics/gpu_compute.h`) is general GPU work: storage
buffers, compute kernels, uploads and read-backs, in order. The SDL
backend gives it on a GPU device of its own (`backends/graphics/sdl3/sdl_compute.cpp`,
SDL's GPU API, Vulkan or Metal), which needs no window, so `reluva-tool`
links it too; each program registers it at start-up
(`gfx::setComputeFactory`). `gfx::Renderer3D` was not used: it draws
fixed-function, into a window's frame, and can't run the Voodoo's pixel
units, read 5:6:5 back for blending or compare 16-bit depths as the board
does.

Nothing under `src/graphics/glide/` knows about NFS.

### The pipeline core

Version-neutral: levels are named by the log2 of their longer side (8 for
256), aspect ratios by log2(width / height) (3 for 8x1), so Glide 2's
`GR_LOD_*`/`GR_ASPECT_*` and Glide 3's log2 constants both convert to it.
The combine, blend, compare, depth, fog and texture format numbers are the
same in both APIs and are used as they are.

- `Board`: the generation (Voodoo Graphics, Rush, Voodoo2, Banshee,
  Voodoo3), TMU count (1-3), texture memory per TMU, frame buffer memory,
  unified memory, revisions, the palette, SLI, the largest resolution,
  windowed Glide and the desktop sharing its memory, texture alignment,
  fog and gamma table sizes, the linear frame buffer's scan line, formats
  and pixel pipeline, Glide 3's extensions (Boards, below). NFS III's board is a Voodoo2 with 4 MB of frame buffer and two
  TMUs of 4 MB (the 12 MB card).
- Frame buffer: three 16-bit 5:6:5 colour buffers (front, back, triple)
  and the 16-bit auxiliary buffer (depth, or alpha), at the resolution
  `grSstWinOpen` asks for. `open()` checks the board's memory as
  Programming Guide table 3.3 does, with the 2D desktop's share on a
  windowed Voodoo Rush (`bufferBytes`, `fits`); `resize()` is
  `GR_CONTROL_RESIZE`.
- Texture memory: each TMU's bytes, its 256-entry palette (or the 6666
  one, `setPalette6666`) and two NCC tables. Downloads write the bytes; sampling reads and widens them, so a
  texture overwritten in memory changes as on the card.
- `State`: everything the Glide state functions set, per TMU where Glide
  has it per TMU.
- `Renderer`: what draws. `makeSoftwareRenderer()` is the reference,
  `makeGpuRenderer()` the GPU backend. The interface: `triangle`, `line`,
  `point`, `fragments` (single pixels each with its values, and the
  primitive's gradients for the texture LOD: the anti-aliased primitives'
  coverage pixels), `clear`; `sync` before the buffers are read or written from
  outside, `syncBuffer` when only one is (a presented frame);
  `textureMemoryChanged` with the bytes, `tablesChanged`, `swapped`. A
  pipeline's colour buffers sit in fixed slots (`slotOf`, `slotPixels`):
  a swap changes which slot is the front, so a renderer keeping copies
  keeps them by slot.
- Output: `present()` widens the front buffer to 8 bits a channel (top
  bits repeated) through the gamma table, one a channel (Glide 2's
  `grGammaCorrectionValue` sets all three, Glide 3's `guGammaCorrectionRGB`
  each), or the table Glide 3's `grLoadGammaTable` loaded: the board's
  entries (32 or 256), read by interpolating between them as the Voodoo2's
  `clutData` describes ("the 3 LSBs ... used to linearly interpolate"). `outputFilter` can replace that step: the
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
   NCC lookups; a P_8 texture through Glide 3's 6666 palette as format 6,
   "8 bit Palette to RGBA"). Clamp, wrap, or with Glide 3's
   `GR_TEXTURECLAMP_MIRROR_EXT` mirror (Boards, Glide 3's extensions).
   Then that TMU's `grTexCombine` with what the one above gives.
5. **Chroma-key** on the colour combine's "other" colour (Reference Manual,
   `grChromakeyMode`), before the combine; with Glide 3's chroma range
   every component within the range instead.
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
  slot, the chroma range; per TMU its format (format 6 for a P_8 texture
  through the 6666 palette), filters, clamps (wrap, clamp or mirror), LOD
  range and bias, mipmap
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
- **Command buffers in flight.** Each batch is sent in a command buffer
  of its own (`flush()`); at most three are on the GPU, the next waiting
  for the oldest. SDL keeps a descriptor-set cache for every command
  buffer in flight at once, for good (see idtech3.md, 3D rendering in
  `System`), and without a wait until the frame's read-back a frame of
  many batches could put as many in flight.
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
feature both ways: of 307,200 pixels 134 differ in the first frame (single
texels at boundaries in the high-frequency checker textures, up to a whole
texel's colour), 7 in the second (a smooth gradient, a dither step), and
1,298 auxiliary buffer values (w-buffer depths one step apart); a third
frame on a Voodoo3 (a P_8 texture through the 6666 palette, point and
bilinear) differs in 140 pixels, again single texels at the boundaries
of a high-frequency index texture. Without a GPU device the suite says so
and passes.

### The Glide 2 front end

`Glide2` holds a `Pipeline` and the application's colour format, and
converts Glide 2's constants. Implemented, with their documented meaning:

- Start-up and the board: `grGlideInit`, `grGlideShutdown`,
  `grGlideGetVersion` ("Glide Version 2.46"), `grSstQueryBoards`,
  `grSstQueryHardware` (a `GrSst96Config_t` for the Rush), `grSstSelect`,
  `grSstWinOpen` (fails for `GR_RESOLUTION_NONE` but on the Voodoo Rush, as
  the Reference Manual says; there it opens in the window, its memory
  shared with the desktop), `grSstWinClose`,
  `grSstControl`/`grSstControlMode` (`GR_CONTROL_RESIZE` resizes a windowed
  Rush's buffers, Boards),
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
  dither, cull and constant colour function; `grGammaCorrectionValue` (a
  no-op on the Rush, "which may lack gamma correction hardware");
  `guColorCombineFunction`, `guAlphaSource`, `grTexCombineFunction` and
  `guTexCombineFunction` as `grColorCombine`/`grAlphaCombine`/
  `grTexCombine` settings that give the effects their Reference Manual
  pages describe (Reluva's reading).
- Fog tables: `guFogTableIndexToW`, `guFogGenerateExp`, `...Exp2`,
  `...Linear`, normalized as their pages say.
- Textures: `grTexMinAddress` (0), `grTexMaxAddress` ("the highest valid
  texture start address and is valid only for the smallest mipmap level
  GR_LOD_1": the start of a 1x1 level's 8-byte unit at the memory's end,
  on the board's texture alignment, so 16 bytes from the end on the
  Banshee and Voodoo3),
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
  `grLfbReadRegion` (5:6:5). The lock's stride is the board's logical
  scan line (1024 pixels; 2048 on the Banshee and Voodoo3; the buffer's
  width on the Rush), and on the Rush the linear frame buffer takes only
  5:6:5 and the auxiliary buffer's 16 bits, without the pixel pipeline
  (Boards).

Texture memory layout: the specification's table (section 10, Texture
Memory Access) gives each level's size, 16-bit levels in 8-byte units with
their sides counted as at least 2 texels and 8-bit levels half that; a
mipmap's levels follow each other from its largest, the even or odd ones
only when split (`evenOdd`). `grTexDownloadMipMap` reads every level from
its data, largest first, rows packed, and keeps those `evenOdd` names.

- Anti-aliasing: `grAADrawTriangle` (the edges asked for),
  `grAADrawLine`, `grAADrawPoint`, `grAADrawPolygon`,
  `grAADrawPolygonVertexList`, `guAADrawTriangleWithClip` (Anti-aliasing,
  below).
- The linear frame buffer's colour formats and swizzles:
  `grLfbWriteColorFormat`, `grLfbWriteColorSwizzle` (Linear frame buffer
  writes, below).
- The utility library: the texture manager, `gu3dfGetInfo`, `gu3dfLoad`,
  `guEncodeRLE16`, `guEndianSwapWords`, `guEndianSwapBytes` (The utility
  library, below).

Stubs (log once, return 0): `grSplash` (3dfx's animation),
`guTexCreateColorMipMap` and `ConvertAndDownloadRle`: the last two are only
a name and a signature in the SDK's headers, no document says what they
do.

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
  caller's buffer is large enough and returning the bytes written (failing
  for `GR_BITS_GAMMA` and `GR_GAMMA_TABLE_ENTRIES` on the Rush, "if gamma
  correction is not available"; `GR_LFB_PIXEL_PIPE` 0 there);
  `grGetString` the extension list (the board's, Boards, or "a single
  space" if none), hardware ("Voodoo3 (tm)",
  "Voodoo2"..., the names NFS III's patch DLL compares, RE `0x600115f0`),
  renderer, vendor and version ("3.10", the version the patch DLL takes
  as current, `0x600115e8`); `grQueryResolutions` every resolution,
  buffer count and auxiliary buffer that fits the board's memory, for
  the template's fields (`GR_QUERY_ANY` for any).
- **Modes.** `grEnable`/`grDisable`: `GR_ALLOW_MIPMAP_DITHER` (until it,
  `GR_MIPMAP_NEAREST_DITHER` is plain nearest, as `grTexMipMapMode`'s page
  says), `GR_AA_ORDERED` (Anti-aliasing, below), the rest kept and logged
  once where they'd change the picture (`GR_VIDEO_SMOOTHING`).
- **Clip coordinates clipped** against the view volume before the
  division by w (Clip coordinates, below).
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
  (c = 255 (c / 255)^(1/γ)); `grLoadGammaTable` loads the board's table
  (its first `nEntries`, "the extra values are discarded").
- **The linear frame buffer.** `grLfbWriteRegion` takes Glide 3's pixel
  pipeline flag before the stride and writes through the pipeline if set.
- **State.** `grGlideGetState`/`SetState` save the core's state and the
  layout and enables under a number kept in the caller's buffer;
  `grGlideGetVertexLayout`/`SetVertexLayout` the layout. `grGet` reports
  64 bytes for each.

`voodoo3Board()`: a Voodoo3 with 16 MB of unified memory (Boards, below):
`grGet(GR_MEMORY_UMA)` 16 MB, `GR_MEMORY_FB` and `GR_MEMORY_TMU` 0 as the
manual says for a UMA configuration; textures from the first 16-byte
boundary after the open window's buffers (`grTexMinAddress`) to 16 bytes
before the end (`grTexMaxAddress`); 256 gamma entries; revisions 1 (a
choice); up to 2048x1536 (a choice); the three extensions.

**`grGet`'s numbers** are the Glide 3.x SDK header's (`glide.h`, `#define
GR_FOG_TABLE_ENTRIES 0x04`, `GR_TEXTURE_ALIGN 0x24`, `GR_VIDEO_POSITION
0x25`, `GR_VIEWPORT 0x26`, `GR_WDEPTH_MIN_MAX 0x27`, `GR_ZDEPTH_MIN_MAX
0x28`; `grEnable`'s `GR_AA_ORDERED 0x01`, `GR_ALLOW_MIPMAP_DITHER 0x02`),
checked against the header again (November 2026), and what each returns
the Reference Manual's grGet table: `GR_VIDEO_POSITION` two values, the
vertical and horizontal beam position ("Vertical retrace is indicated by
y == 0"); `GR_VIEWPORT` four, x, y, width, height; `GR_FOG_TABLE_ENTRIES`
one, "The number of entries in the hardware fog table". So the patch
DLL's `grGet(4)` is the fog table's size and its `grEnable(2)` allows
mipmap dithering, as read before.

**The version string**: the manual gives "3.0" as an example only. NFS
III's patch DLL compares it from its first digit: below "3.01" the board
isn't offered (`THRASH_is`, `0x60001bf0`, the string at `0x6001163c`), up
to "3.10.9" it rates the card 100 and above it 95 (`0x60011634`), and
below "3.10" it shuts Glide down at every close (`0x6000194a`, flag
`0x60017dc0`). Reluva answers "3.10", the lowest that meets all three: a
choice inside the DLL's own ranges. `GR_HARDWARE`: the manual lists
"Voodoo Graphics", "Voodoo Rush", "Voodoo2" and "Banshee"; the patch DLL
looks for "Voodoo Banshee" and "Voodoo3 (tm)" (RE, `0x600115f0`), which
Reluva answers for those two. For the Banshee the documents disagree with
the DLL: the 3.0 manual's list has "Banshee" (and no Voodoo3 at all), the
patch DLL, built in 1999 against a later Glide (it takes 3.10 as current),
compares "Voodoo Banshee", the name the Glide 3.0 release notes also use
("Glide 3.0 supports ... Voodoo Banshee"). Reluva answers the later name,
as it answers version 3.10 (a choice, in Open questions).

**Extensions** (Glide 3.0 Programming Guide, chapter 13; Reference
Manual, grGetProcAddress and grGetString), offered where the board's
specification has the hardware (Boards): `CHROMARANGE`
(`grChromaRangeModeExt`, `grChromaRangeExt`, handed out by
`grGetProcAddress`; the range "order-insensitive", inclusive,
`GR_CHROMARANGE_RGB_ALL_EXT` only), `TEXMIRROR`
(`GR_TEXTURECLAMP_MIRROR_EXT` in `grTexClampMode`) and `PALETTE6666`
(`GR_TEXTABLE_PALETTE_6666_EXT` in `grTexDownloadTable`: each entry
given as 0xAARRGGBB, its channels' top 6 bits kept, "expanded to 8 bits
... by replicating the two high-order bits" when read; Reluva's reading
of the entry's layout). `TEXCHROMA` and `FOGCOORD` aren't offered: no
board here has texture chroma-range or fog-coordinate registers in its
specification.

Stubs (log once, return 0): `grSplash`; `grGetProcAddress` for anything
but the board's extension procedures.

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

### Anti-aliasing

`Pipeline::aaTriangle`, `aaLine`, `aaPoint` (`antialias.cpp`). What the
documents say: Glide draws anti-aliased primitives "by setting up the
alpha iterator so that it represents pixel coverage"; the vertex alpha
"is multiplied by the pixel coverage to obtain the final alpha value for
alpha blending"; the application sets the alpha combine to iterated alpha
and the blending (Programming Guide 2.4, Chapter 4, Anti-aliasing, and
Example 4.2; Reference Manual, grAADrawTriangle, grAADrawLine,
grAADrawPoint). A point is "four pixels, each blended according to the
computed pixel coverage"; lines come out "somewhat fatter than expected";
a steep colour gradient shows "visual anomalies at the edges" (the
colours carried on past the edge). Glide 3: `grEnable(GR_AA_ORDERED)`
makes `grDrawTriangle` "anti-alias all three edges", `grDrawLine` and
`grDrawPoint` anti-aliased too, `grDrawVertexArray(GR_POLYGON)` the
replacement for `grAADrawPolygon`, "all primitives except strips and
fans"; `grAADrawTriangle` "is independent of the GR_AA_ORDERED mode".

Reluva's reading, the same in software and on the GPU:
- **Triangles**: the pixels `grDrawTriangle` would draw, drawn so (vertex
  alpha as it is); then, outside each edge asked for, a fringe: every pixel
  whose centre is within a pixel of the edge's line and outside the
  triangle, its values the triangle's planes carried on to its centre and
  its alpha times the coverage, 1 − the distance (the linear iterator:
  1 on the edge, 0 a pixel out). Outside two anti-aliased edges (by a
  corner) the two coverages multiply; outside one that isn't asked for,
  nothing. Edges are those of the snapped vertices, as the triangle's.
- **Polygons** (`grAADrawPolygon`, `...VertexList`,
  `guAADrawTriangleWithClip`, Glide 3's `GR_POLYGON`): a fan whose outside
  edges only are anti-aliased ("All exterior polygon edges are
  anti-aliased"), so no fringe falls on the polygon's own pixels.
- **Lines**: every pixel whose centre projects onto the segment and lies
  within a pixel of it, coverage 1 − the distance: a line through pixel
  centres covers its row only, one between two rows half each (the
  "fatter" line).
- **Points**: the four pixels a pixel-sized square centred on the point
  overlaps, each with its share of the square (they sum to the vertex
  alpha).
- **Clipped** (Glide 3's clip coordinates through the view volume, Clip
  coordinates below): the polygon left after clipping, drawn as above
  with a fringe outside each piece of an edge asked for and outside each
  edge the clipping made (Reluva's choice, after the documents' own
  clipped anti-aliased triangle: `guAADrawTriangleWithClip` "draws the
  resultant polygon with grAADrawPolygonVertexList", "All edges of the
  clipped triangle will be anti-aliased", Glide 2.4 Reference Manual and
  Programming Guide, Anti-aliasing). A `GR_POLYGON` is clipped whole, so
  the fan's inner edges stay without one.
- `guAADrawTriangleWithClip` (Glide 2) is `grAADrawTriangle` with every
  edge: clipping to the clip window and anti-aliasing the polygon's
  edges gives the same pixels, as the fringe outside an edge on the clip
  window lies outside the window, which the hardware drops.
- The coverage pixels go to `Renderer::fragments`: the software
  rasterizer shades each with the triangle's gradients for the texture
  LOD; the GPU backend sends each as a one-pixel primitive whose plane is
  based at the pixel's centre (the kernel unchanged).

`tests/test_glide_gpu.cpp`'s scene has an anti-aliased triangle, line and
point: their pixels are the same on the GPU as in software (the clipped
fringe goes through the same `fragments`, so it is too).

### Linear frame buffer writes

Every write mode in every colour format (Glide 2.4 Programming Guide
tables 11.2 and 11.3; the Voodoo2 specification's `lfbMode` tables, which
agree): 5:6:5 with red or blue high; 5:5:5 and 1:5:5:5 with the colour in
bits 14-0 and alpha at 15 (ARGB, ABGR) or in 15-1 with alpha at 0 (RGBA,
BGRA); 8:8:8 and 8:8:8:8 in the four orders; the depth modes with the
colour in the high 16 bits and the depth in the low ("the two most
significant bytes contain 565 RGB data", Reference Manual, grLfbLock).
The order is the window's (`grSstWinOpen`'s `cformat`) until
`grLfbWriteColorFormat` sets another, until the next `grSstWinOpen`;
`grLfbWriteRegion`'s sources take it too.

`grLfbWriteColorSwizzle(swizzleBytes, swapWords)`: the manuals give only
its name; its effect is the `lfbMode` register's bits 12 and 11 in the
Voodoo2 specification, in its order: "byte swizzling is performed first",
reversing the four bytes of each 32-bit word written, then the two 16-bit
halves swapped ("never performed ... when data formats 4 and 5 are
used"). Through a lock that is per 32-bit word: two pixels of a 16-bit
mode at once, the left in the low half by default.

Bypassing the pixel pipeline, a mode with alpha (1555, 8888, 1555 with
depth) also writes the alpha into the alpha buffer when alpha buffering is
on (depth buffering off, the alpha mask on), and 1555 with depth then
leaves the depth ("the depth component is ignored", the specification).

### The utility library

**The texture manager** (Reference Manual 2.4, guTexAllocateMemory to
guTexSource): `Glide2` places each mipmap in its TMU's memory from
`grTexMinAddress` up, in the size `grTexTextureMemRequired` gives for its
even/odd levels, moved to the 2 MB line when it would straddle it ("a
mipmap level cannot straddle the 2Mbyte boundary", Programming Guide,
Texture Memory), and returns `GR_NULL_MIPMAP_HANDLE` when it doesn't fit
("If the texture memory can not be allocated"). Handles count from 0.
`guTexChangeAttributes` takes −1 for "not changed" and refuses a mipmap
that no longer fits its memory (Reluva's reading of "a section of texture
memory to be reused"). `guTexDownloadMipMap` downloads every level,
largest first, and keeps an NCC format's table; `guTexDownloadMipMapLevel`
one level, moving the caller's pointer on ("updates *src to point to the
next mipmap level"); `guTexSource` makes it current with all its
attributes ("all of its attributes take effect": clamp, filter, mipmap
mode, LOD bias, trilinear) and its NCC table as table 0 (Reluva's
reading); `guTexGetCurrentMipMap`, `guTexMemQueryAvail` (only the
manager's own allocations, as the page says), `guTexMemReset` (every
handle gone). `guTexGetMipMapInfo` returns a `GrMipMapInfo` (`glide.h`'s
layout, 196 bytes) in guest memory, its register images (`tLOD`,
`tTextureMode`) 0: no document gives Glide's.

**`.3df` files** (`gu3dfGetInfo`, `gu3dfLoad`): the TexUS guide's format,
ASCII lines then the texels: `3df v<major>.<minor>`, comment lines
starting `#`, `color format: <name>` (its table's names: `i8`, `a8`,
`ai44`, `yiq`, `rgb332`, `rgb565`, `argb8332`, `argb1555`, `ayiq8422`,
`argb4444`, `ai88`; `argb8888` "for archiving purposes only" is refused),
`lod range: <min> <max>`, `aspect ratio: <w> <h>`; then the levels, each
"left to right, top to bottom", 16-bit texels with "the leftmost byte in
the color specification" first (big-endian, swapped for the host).
Reluva's readings where the guide is short: the LOD range's numbers are
the smallest and largest levels' sizes in texels; the levels are largest
first ("stored in order from low to high": low LOD numbers, as Glide
numbers them, are the large levels); files of the NCC formats (`yiq`,
`ayiq8422`) are refused, as the guide doesn't say how they hold their
table. `Gu3dfInfo` (`glideutl.h`): the header's LODs and aspect in Glide
2's constants, or Glide 3's log2 ones for `glide3x.dll`; `mem_required`
the texels' bytes; `gu3dfLoad` fills `info->data`. The host reads the
file (`GlideDll::readFile`; NFS III's host by the program's own paths).

**`guEncodeRLE16(dst, src, width, height)`**: the 16-bit image as
`grLfbWriteRegion`'s `GR_LFB_SRC_FMT_RLE16` takes it ("one 16-bit count
word and one 16-bit color word", the count signed), runs ending at each
row's end and at 32,767; it returns the bytes, and with a null `dst` only
counts them (the return value and the null are Reluva's choice: the
header gives only an `int`). **`guEndianSwapWords`** and
**`guEndianSwapBytes`**: the 32-bit value's halves, the 16-bit value's
bytes, as the names say.

### Boards

`Board` and its makers (`voodoo1Board()`, `voodooRushBoard()`,
`voodoo2Board()`, `bansheeBoard()`, `voodoo3Board()`; `boardByName("voodoo1,
tmu=4")` with `tmu=`, `fb=` (MB), `tmus=`, `tmurev=`):

| | Voodoo Graphics | Voodoo Rush | Voodoo2 | Banshee | Voodoo3 |
|---|---|---|---|---|---|
| TMUs | 1 | 1 | 2 | 1 | 2 |
| Texture memory | 2 MB | 2 MB | 4 MB each | unified | unified |
| Frame buffer | 2 MB | 4 MB, shared with the desktop | 4 MB | 16 MB with the textures | 16 MB with the textures |
| Largest screen | 800x600 | 1600x1200 | 1024x768 | 1600x1200 | 2048x1536 |
| Palette | TMU revision 1 on | no | yes | yes, and 6666 | yes, and 6666 |
| `GR_RESOLUTION_NONE` | fails | the window | fails | fails | fails |
| Texture alignment | 8 | 8 | 8 | 16 | 16 |
| Gamma entries | 32 | none | 32 | 256 | 256 |
| Lock stride (pixels) | 1024 | the buffer's width | 1024 | 2048 | 2048 |
| LFB formats, pixel pipeline | all, yes | 5:6:5 and ZA16, no | all, yes | all, yes | all, yes |
| Glide 3 extensions | none | none | CHROMARANGE, TEXMIRROR | CHROMARANGE, TEXMIRROR, PALETTE6666 | the same |
| Revisions | 2 | 2 | 2 | 1 | 1 |
| `grSstQueryHardware` type | SST-1 (0) | SST-96 (1) | Voodoo2 (3) | Banshee (4) | Banshee (4) |

From the documents:
- **Voodoo Graphics**: "2-4 MBytes of EDO DRAM frame buffer memory",
  "1-4 MBytes ... texture memory" (SST-1 specification, Features); the
  resolutions of Programming Guide table 3.3 (2 MB: 800x600 double
  buffered, 640x480 with a depth buffer or three buffers), which
  `open()`'s memory check gives; `Revision_ID` 2 (the specification's
  last). The 2 + 2 MB card is the common one; `tmu=4` the 6 MB one. On
  Pixelfx revision 1 the compare-to-bias depth modes "are not available"
  (Programming Guide, Depth Buffering): they fall back to plain z or w
  (only with `fbiRevision` 1). On TMU revision 0 the palette is "not
  implemented" (Programming Guide, The Color Palette):
  `grTexDownloadTable(GR_TEXTABLE_PALETTE)` is ignored and logged.
- **Voodoo Rush** (SST-96 specification): "1 to 4 MB of texture memory",
  a frame buffer shared with the 2D chip, "any resolution which fits in
  the frame buffer memory, is less than 2K by 2K"; its texture formats
  leave out the 8-bit palette ones, so no palette. `grSstWinOpen`'s
  `GR_RESOLUTION_NONE` "is permitted for the SST-96. This signals Glide
  to use the user specified window" (Reference Manual): the window's size
  from the host (`Glide2::windowSize`), 640x480 if none.
  - **The desktop's memory** (SST-96 specification, 1.3 System
    Resolutions): "Full-screen 3D requires the memory space of the
    front/back 3D buffers, while windowed 3D requires the additional
    memory space of the 2D desktop", the window's buffers off screen:
    "two off-screen buffers (out-of-place 3D) or one on-screen buffer and
    one off-screen buffer (in-place 3D -- requires equal 2D and 3D color
    depths)". Its tables' totals are the desktop plus those buffers (a
    16-bit 1024x768 desktop and a 640x480 window in place: 1,572,864 +
    614,400 = 2,187,264; with a 16-bit depth buffer on a 1280x1024 one,
    3,850,240; an 8-bit 1152x864 one out of place, 2,224,128). So
    `Pipeline::fits` counts, for `GR_RESOLUTION_NONE`, the desktop, the
    colour buffers but the front on a 16-bit desktop (all of them on an
    8-bit one) and the auxiliary buffer, against the 4 MB; a named
    resolution is full screen, the buffers alone. The window can't be
    larger than the desktop. The desktop is 1024x768 at 16 bits, the
    first row of the in-place table (a choice; `boardByName("rush,
    desktop=1280x1024x16")` or `desktop=0`), unless the host knows its
    own: the Play page passes the display's desktop mode (SDL's) as
    `desktop=` when reluva.ini's `glideboard=` names the Rush without
    one; nfs-host keeps the default unless `-glideboard` gives one.
    `setDesktop` takes it. The specification's windowed tables have
    only 8- and 16-bit desktops (out of place and in place), of
    1024x768 to 1600x1200 at 8 bits and to 1280x1024 at 16, and a
    1600x1200 16-bit desktop would leave a 4 MB board almost nothing.
    A desktop a Rush couldn't have had is made one it could (a choice):
    15 bits counts as 16, a true-colour (24- or 32-bit) one as 16 (what
    its owner would have set to play in a window: in place needs "equal
    2D and 3D color depths"), a width or height past the tables' largest
    at its depth is cut to it (a 2560x1440 32-bit desktop is a 16-bit
    1280x1024 one), and one whose memory alone would fill the frame
    buffer drops to the next smaller of the tables' sizes (`fb=2`: 16-bit
    1152x864). Smaller desktops (800x600) are kept; the window just
    can't be larger. The Play page says so when it changes the
    desktop. That is what the Reference
    Manual's `num_aux_buffers` says: "For a windowed application running
    on SST-96, this parameter allows an application to run in a larger 3D
    window if a depth buffer is not necessary" (grSstWinOpen). Some of
    the tables' totals are misprinted (2,105,232 for 786,432 + 1,228,800;
    2,492,264 for 2,492,464...); the sums are used. The command FIFO
    lives in what is left ("CMDFIFO Space", 1 to 512 4 KB pages, section
    3): Glide's share of it isn't documented, so nothing is held back for
    it (a choice).
  - **Resizing**: `grSstControlMode(GR_CONTROL_RESIZE)` "resizes the back
    buffers and auxiliary buffers ... may fail if there is not enough
    offscreen video memory to accommodate the resized buffers", ignored
    "in full screen mode"; `GR_CONTROL_MOVE` validates the window's place
    (Glide 2.4 Reference Manual, grSstControlMode). Here RESIZE asks the
    host for the window's size again and reallocates the buffers if they
    fit (`Pipeline::resize`, the state kept, the clip window the whole new
    window: a choice), else fails with nothing changed; MOVE succeeds.
  - **What Glide reports and allows**: `grSstQueryHardware` gives
    `GR_SSTTYPE_SST96` with the header's `GrSst96Config_t` (fbRam, the
    shared 4 MB; nTexelfx; one `GrTMUConfig_t`), no Pixelfx revision or
    SLI field. The linear frame buffer goes "through 2D engine" and the
    buffers are "separate linearly packed buffers" (SST-96 specification,
    9. Changes from SST-1) with an X stride in quad pixels
    (colBufferSetup): a lock's stride is the buffer's width rounded up to
    4 pixels (Reluva's reading), the formats the buffers' own (5:6:5,
    ZA16; `GR_LFBWRITEMODE_ANY` gives those, as the Reference Manual says
    for the Rush), and no pixel pipeline: the Reference Manual's grLfbLock
    "If the destination pixel format specified is not supported on the
    target hardware, then the lock will fail", "If the underlying hardware
    is incapable of processing pixels through the pixel pipeline, then the
    lock will fail", and the 3.0 manuals name the Rush: "Not all hardware
    supports pixel pipe writes or source formats (e.g. Voodoo Rush);
    grLfbWriteRegion() will return FXFALSE" (Programming Guide 3.0,
    chapter 11; Reference Manual 3.0, grLfbWriteRegion). RLE16 is still
    taken (5:6:5 once expanded: a choice). `grGammaCorrectionValue` "is a
    no-op on SST-96 systems, which may lack gamma correction hardware"
    (Reference Manual 2.4), so no gamma table; Glide 3's `grGet` fails for
    `GR_GAMMA_TABLE_ENTRIES` and `GR_BITS_GAMMA` there and answers
    `GR_LFB_PIXEL_PIPE` 0. No Glide 3 extensions ("not available on
    systems with Voodoo Graphics and Voodoo Rush hardware").
- **Voodoo2**: as before (NFS III's 12 MB card); its 33-entry `clutData`
  table indexed by "the 5 MSBs" gives 32 gamma entries. Its Features list
  "Texture coordinate clamping, wrapping, and mirroring" and "Source and
  Destination Chroma-range functionality" (the `chromaRange` register):
  Glide 3's `TEXMIRROR` and `CHROMARANGE` (the Programming Guide: "newer
  hardware (like Voodoo2), supports all of the extensions"; its texture
  formats have no 6666 palette, so not `PALETTE6666`).
- **Banshee, Voodoo3**: the Voodoo3 specification's memory is one: "The
  amount of texture memory is only limited by the maximum amount of ...
  frame buffer memory", 4-16 MB; Glide 3's grGet describes such a "UMA
  configuration" (`GR_MEMORY_UMA` the total, `GR_MEMORY_FB` and
  `GR_MEMORY_TMU` 0). Textures start at the first 16-byte boundary after
  the open window's buffers ("texbaseaddr ... in 16-byte units", which
  `grGet(GR_TEXTURE_ALIGN)` reports); every TMU reads the one memory (a
  download into TMU0 is TMU1's too). The video's "Triple 256x8 lookup
  tables" give 256 gamma entries. Its "Modifications from SST1" list
  chroma range, "Texture mirroring" and the "Additional 6666 palettized
  texture format": the three extensions.
- **Banshee** (its specification, revision 1.1, November 1998): one
  memory of "4 Mbytes, with a maximum of 16 Mbytes" for "RGB,
  alpha/depth-buffer, 2D desktop, video, and texture memory", "The amount
  of texture memory is only limited by the maximum amount of Banshee frame
  buffer memory" (3.3 Functional Overview, Memory Architecture): unified,
  16 MB (the most; a choice of card). One TMU: "Combined TMU unit"
  (Modifications from SST1), TREX0 alone in the FBI/TMU diagram (3.2.3).
  Texture bases "in 16-byte units" (texBaseAddr). A "64-entry lookup
  table" for fog. Texture formats with "PalettedRGBA (8 expanded to ARGB
  6-6-6-6)" (format 6, "8 bit Palette to RGBA", textureMode's table),
  tLOD's tmirrors and tmirrort, the chromaRange register: the three
  extensions. Gamma through the RAMDAC's CLUT, 24 bits an entry, "the
  lower 256 entries" for the desktop (vidProcCfg, dacData): 256. Its
  "high speed 220 Mhz ramdac, capable of doing 1600x1280 @ 76Hz": up to
  1600x1200 here (the largest of Glide's resolutions within it).
  Revision_ID "1=.35u, 2 = .25u": 1. Its linear frame buffer: "all linear
  frame buffer accesses assume a 2048-pixel logical scan line width"
  (Linear Frame Buffer Access), so a lock's stride is 4,096 bytes for the
  16-bit modes and 8,192 for the 32-bit ones; the same on the Voodoo3,
  whose specification has the same sentence. (Both go on to say "2048
  bytes per logical scan line" for the 16-bit formats, the Voodoo2's
  wording; the section is marked "(* FIX THIS *)" in the Banshee's. The
  2048-pixel line is taken: resolutions over 1024 wide need it.) Glide 2
  calls it `GR_SSTTYPE_Banshee` (4), with one TMU of the unified memory
  (what Glide 2 reports for a UMA board's tmuRam isn't documented: the
  whole memory, a choice). The specification's own additions that Glide
  doesn't reach (separate RGB and alpha filters, the "blending fraction
  from 1.4 to 1.8", the 2D engine, video overlay) aren't modelled.
- **The SST-1's snapped coordinates**: NFS III's `voodooa.dll` adds
  3 × 2^18 to x and y and passes the sums to `grDrawTriangle` without
  taking it off again (RE, `THRASH_drawtri` `0x60003610`: `fadd
  [0x60010148]`, 786,432.0) — the Programming Guide's vertex snapping
  (Example 14.1) with its second half left out. Such a float's last bit is
  1/16, so its low 16 bits are the coordinate in 12.4 two's complement,
  what the SST-1's integer `vertexAx` registers take ("Bit 15:0 ... fixed
  point two's complement 12.4 format"). On the Voodoo Graphics and Rush,
  `Glide2` reads a coordinate in [2^19, 2^20) so (Reluva's reading of how
  that board's Glide took them); others as they are.

### Clip coordinates

Glide 3's `GR_CLIP_COORDS` vertices are divided by w and put through the
viewport and depth range as before; first they are clipped against the
view volume the manuals describe (x/w and y/w "normalized device
coordinates", "z should be in the range [−w..+w]", Programming Guide 3.0,
Coordinate Spaces): w > 0, −w ≤ x ≤ w, −w ≤ y ≤ w, and −w ≤ z ≤ w when the
layout has z. Triangles are clipped plane by plane (Sutherland-Hodgman)
and drawn as fans; lines are cut; points outside are dropped. Every
parameter (colour, alpha, z, w, q, each TMU's s, t and q, before the
division) is interpolated linearly in clip space. A primitive wholly
inside is drawn exactly as before. The manuals themselves leave geometric
clipping to the application ("proper geometric clipping should be done
by the application", grClipWindow); clipping here keeps a vertex behind
the eye from being divided through (a choice, the volume the documents').
An anti-aliased triangle (`grAADrawTriangle`, or any with `GR_AA_ORDERED`)
or polygon that had to be clipped keeps its fringe: each piece of an edge
asked for has it, and so does each edge the clipping made (Anti-aliasing,
above).

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
`voodoo3Board()` (or the board `-glideboard` names). It looks up 55 Glide
3 functions, all carried out. It identifies the board by
`grGetString(GR_HARDWARE)`, takes its buffer counts from
`grQueryResolutions` (for every Glide 3 board it takes the
`GR_MEMORY_UMA` path, whatever the value: thrash.md), opens 640x480 with two buffers and
a depth buffer, sets the layout of the game's own 32-byte vertex (window
coordinates, packed ARGB) and draws every triangle with `grDrawTriangle`.
It runs the menus, loading screens and races; what it asks for, and that
it draws the game without fog, is in
[thrash.md](../formats/nfs/thrash.md), The Voodoo3 patch's voodoo2a.dll.

## NFS III's voodooa.dll (Voodoo Graphics)

The install's `voodooa.dll` (the module 3D Device Setup gives a Voodoo
Graphics card; Watcom, as the retail `voodoo2a.dll`) runs unchanged on the
Glide 2 stand-in on a Voodoo Graphics board: `nfs-host -glidedll
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

`tests/test_glide_more.cpp` (the `glide_more` suite): every linear frame
buffer write mode in the four colour formats, `grLfbWriteColorFormat`
and `grLfbWriteColorSwizzle` (directly and through a guest lock), alpha
into the alpha buffer; anti-aliased triangles (with every, no and one
edge, either winding), points on a centre and a corner, lines through and
between pixel centres, a polygon's outside edges, Glide 3's
`GR_AA_ORDERED`; the texture manager (placement, attributes on
`guTexSource`, downloads, the 2 MB line, a full TMU, reset) and its
binding; a `.3df` file made in the test (header, texels' byte order,
refusals), RLE16 encoded and read back by `grLfbWriteRegion`, the endian
swaps, `gu3dfGetInfo`/`gu3dfLoad` and `guEncodeRLE16` through the
interpreter with a host file reader; the boards (sizes and resolutions,
`GR_RESOLUTION_NONE` on the Rush, no palette, the Voodoo3's unified
memory and its grGet answers, `grLoadGammaTable`, `boardByName`); clip
coordinates clipped (behind the eye, beyond the far plane, half through
it, a line through w = 0, a point outside).

`tests/test_glide_boards.cpp` (the `glide_boards` suite): the Rush's
desktop memory against the SST-96 specification's table totals, the
host's desktop made one a Rush could have had (`setDesktop`: true colour,
too large, too large for a 2 MB board, smaller kept, other boards
untouched), a window that fits only without a depth buffer, full screen against windowed,
`GR_CONTROL_RESIZE` (taken, refused for want of memory, ignored full
screen), the Rush's locks and `grLfbWriteRegion` (formats, pixel
pipeline, the packed stride), its gamma no-op and failing `grGet`s, its
`GrSst96Config_t` through the interpreter; the Banshee's configuration,
largest screen, `grGet`/`grGetString` answers and 2048-pixel lock
stride; the extension strings by board ("a single space" for none), the
6666 palette (packing, format 6, its alpha drawn, a plain palette after
it, refused on a Voodoo2), mirrored textures (and wrapped where the
board has no mirroring), the chroma range (inclusive, either order,
the key alone again once off), `grGetProcAddress` through the
interpreter; the fringe of clipped anti-aliased triangles (outside the
original edges and the clipped one, only the edges asked for,
`GR_AA_ORDERED`, unchanged when nothing is clipped, a clipped
`GR_POLYGON` without inner fringes).

`tests/test_glide_gpu.cpp` (the `glide_gpu` suite): the GPU backend
against the software reference (The GPU backend, above), anti-aliased
primitives, mirrored textures, the chroma range and (on a Voodoo3) the
6666 palette included.

## Compared with the native THRASH and softtria

See [thrash.md](../formats/nfs/thrash.md), The Glide stand-in, for runs,
numbers and the causes of the differences.

## Extension points

- **Glide 3's extensions**: `TEXCHROMA`, `FOGCOORD`
  (`GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT`) and later ones
  (`GR_TEXFMT_ARGB_CMP_FXT1`...) aren't offered, each logged once if
  asked for; the chroma range's other modes (the registers' exclusive
  ranges and unions) have no Glide 3.0 constant.
- **Boards**: SLI is a flag for the memory checks only. The Rush's
  desktop is a fixed size (`desktop=`), not the host's. `Board::fogTableEntries` is where a board
  with a larger fog table would go (Glide 3's front end resamples to the
  core's 64).
- **TMUs**: up to three; the rasterizer already chains them
  (`grTexCombine` with other). NFS III's patch DLL would load every
  texture into TMU1 too, but only when `VOODOO2_NUMTMU` says 2 or more
  (thrash.md).
- **The GPU backend** could take the planes' arithmetic closer to the
  software one (each row's start, then stepped), at the cost of a serial
  loop per row; and count pixels for the statistics with atomics.
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
  bit 19) is not done. Left as it is on purpose (November 2026): settling
  it needs frames from a real card, or the user's decision.
- The blend's rounding: the combine units' `(c × (f + 1)) >> 8` is used.
  The Voodoo2 specification gives only Dnew = S·α + Dold·β; the Banshee's
  "Increased blending fraction from 1.4 to 1.8" says the Banshee's is
  finer than the earlier boards', not how either rounds.
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
- Glide 3: a disabled parameter keeps its last value; a TMU without its
  own s and t takes TMU0's; 64 bytes for the state and layout buffers;
  "3.10" as the version string (inside the patch DLL's ranges); one
  refresh rate per resolution in `grQueryResolutions`; the Voodoo3's
  revisions (1) and largest screen; where textures start on a UMA board
  (the first 16-byte boundary after the buffers); "Voodoo Banshee" for
  `GR_HARDWARE` (the 3.0 manual says "Banshee"; the patch DLL compares
  the longer name).
- Boards: the Rush's desktop (1024x768, 16 bits, when the host doesn't
  give its own; a host desktop the Rush couldn't have had made one it
  could: true colour as 16 bits, larger cut to the tables' largest),
  nothing kept back for
  its command FIFO, its lock stride (the width in quad pixels), RLE16
  still taken there, `GR_CONTROL_RESIZE` keeping the state; the Banshee's
  16 MB (the specification allows 4-16), its 1600x1200 (from its RAMDAC),
  the 2048-pixel scan line (its specification contradicts itself), the
  tmuRam Glide 2 reports for a UMA board.
- Glide 3's extensions: a 6666 palette entry given as 0xAARRGGBB (the
  manuals give the stored 6:6:6:6 but not the argument's layout), a
  mirrored index (the texel index reflected, so bilinear filtering takes
  the edge texel twice at the seam), which board has which (from each
  specification's registers).
- Anti-aliasing: the coverage as 1 − the distance from the edge (the
  manuals say only that the alpha iterator stands for coverage); a line's
  pixels within a pixel of it; a fringe along edges made by clipping to
  the view volume (as the documents' `guAADrawTriangleWithClip` does for
  the clip window).
- `grLfbWriteColorSwizzle`'s meaning (from the Voodoo2's `lfbMode`),
  relative to Glide's default packing.
- The texture manager: handles from 0, an NCC table made current with its
  mipmap, `guTexChangeAttributes` refusing a mipmap grown past its memory,
  `GrMipMapInfo`'s register images left 0.
- `.3df`: the LOD range as sizes, levels largest first, NCC files refused.
- `guEncodeRLE16`: runs within rows, the return value, a null `dst`.
- The SST-1's snapped coordinates read by their low 16 bits.
- Clip coordinates: clipped at all (the manuals leave it to the
  application).

Settled from the documents (November 2026), no longer choices:
- `grTexMaxAddress`: "the highest valid texture start address ... valid
  only for the smallest mipmap level GR_LOD_1" (Reference Manual 2.4):
  the memory less a 1x1 level's 8 bytes, on the board's alignment (less
  16 on the Banshee and Voodoo3, where it was less 8).
- `grGetString(GR_EXTENSION)` with none: "a single space" (Reference
  Manual 3.0, grGetString), where it was empty.
- The Banshee is no longer a one-TMU Voodoo3 by assumption: its own
  specification gives its memory, TMU, formats, fog, gamma, revision,
  scan line and largest screen (Boards).
- The Rush's memory shared with the desktop, `GR_CONTROL_RESIZE`, and
  what its linear frame buffer and gamma allow (Boards).
