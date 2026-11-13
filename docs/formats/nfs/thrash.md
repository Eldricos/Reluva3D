# Need for Speed III: THRASH, the renderer interface

Status: **partly implemented**. Native drawing is in
`src/engines/nfs/host/thrash_native.cpp`; `reluva-tool nfs-host -native`
uses it. The renderer DLL still keeps its own books (textures, windows,
page flips). Native drawing takes over only its triangles, quads, lines
and points.

THRASH is the interface between `nfs3.exe` and its renderer DLLs:

- `softtria.dll` is the software renderer;
- `d3da.dll` runs on Direct3D;
- `voodooa.dll` and `voodoo2a.dll` run on Glide (`glide2x.dll`, loaded
  by name), for 3dfx's Voodoo cards. The Classic CD's Voodoo3 patch
  replaces `voodoo2a.dll` with one built for Glide 3 (see The Voodoo3
  patch's voodoo2a.dll). `voodoo2a.dll` runs unchanged under
  `nfs-host -glide` on Reluva's Glide stand-in (The Glide stand-in,
  below; docs/common/graphics/glide.md).

The game loads the one the registry names ("Thrash Driver") and finds
its functions by name with GetProcAddress. Each export is
`_THRASH_<name>@<argument bytes>`, stdcall.

## How it was worked out

October 2026, from the retail DLLs and the game running in Reluva's
interpreter (Phase 4's host). This is reverse engineering for
interoperability.

- **Calls and arguments.** Every THRASH call was logged over a run of
  frames, with its arguments and the memory its pointer arguments point
  at (`nfs-host -thrashlog from to`). The menus and a race were both
  logged.
- **Render states.** `d3da.dll` turns each `setstate` id into Direct3D
  render states. Disassembling that mapping names most ids, because the
  Direct3D states it sets are documented.
- **Vertices.** The 32 bytes each vertex argument points at hold floats at screen
  scale, then colours. d3da hands vertices straight to Direct3D's
  DrawPrimitive as D3DTLVERTEX, the documented layout, with no
  conversion.
- **Texture formats.** From softtria's `tupdate`: a jump table by format
  leads to a converter for each format. Each converter was disassembled:
  - which bits it reads;
  - how it scales them;
  - which values it treats as transparent.
- **Fog.** `voodoo2a.dll`'s `setstate` was disassembled, with the
  game's code that sends the fog states and picks the fog mode. The
  meaning of each step is Glide's, whose functions it finds by name.
  Checked with a copy of the game whose `.hrz` files were edited to red
  fog: the game sends the states as worked out, and the frames fog red
  with distance (see Fog).
- **Window buffers.** softtria draws into buffers of its own, not into
  the DirectDraw surface; `pageflip` copies them across. `lockwindow`
  returns where they are.
- **Checks.** Natively drawn frames were compared with softtria's frames
  of the same menu screen:
  - mean difference 1.7 of 255 per channel;
  - the rest is edge pixels of translucent artwork (rounding in the
    blend).

  A race (Hometown, 8 cars) draws the same scene as softtria: road,
  scenery, cars, the dashboard and the HUD.

## Vertices

32 bytes, Direct3D's D3DTLVERTEX:

| Offset | Type | Meaning |
|---|---|---|
| 0 | float | x, in pixels |
| 4 | float | y, in pixels, down |
| 8 | float | z |
| 12 | float | 1/w, for perspective-correct texturing |
| 16 | u32 | diffuse colour, 0xAARRGGBB |
| 20 | u32 | specular colour |
| 24 | float | u, 0..1 across the texture |
| 28 | float | v, 0..1 down the texture |

## Drawing

| Function | Arguments | Draws |
|---|---|---|
| `drawtri` | a, b, c | a triangle; each argument points at a vertex |
| `drawquad` | a, b, c, d | a quad: triangles (a, b, c) and (a, c, d) |
| `drawtrifan` | n, vertices | n triangles over n + 2 consecutive vertices, sharing the first |
| `drawtristrip` | n, vertices | n triangles over n + 2 consecutive vertices |
| `drawtrimesh` | n, vertices, indices | n triangles; indices are u32, 3 each |
| `drawquadmesh` | n, vertices, indices | n quads; 4 indices each |
| `drawline` | a, b | a line |
| `drawlinestrip` | n, vertices | n lines over n + 1 vertices |
| `drawlinemesh` | n, vertices, indices | n lines; 2 indices each |
| `drawpoint` | a | a point |
| `drawpointstrip` | n, vertices | n points |
| `drawpointmesh` | n, vertices, indices | n points; 1 index each |

## Textures

- `talloc(width, height, format, 0, 0)` returns a handle.
- `tupdate(handle, pixels, palette)` gives the texels, the palette, or
  both.
- `treset()` drops them all.
- `setstate(1, handle)` picks the texture to draw with; 0 picks none.

| Format | Texel | Notes |
|---|---|---|
| 2 | 8 bits, an index | `palette` points at 256 u32 colours, 0x00RRGGBB; either may come alone. Index 255 is left out (softtria marks a texture that uses it as transparent) |
| 3 | 16 bits, 1:5:5:5 | the top bit set where opaque; texels without it are left out |
| 4 | 16 bits, 5:6:5 | opaque |
| 7 | 16 bits, 4:4:4:4 | alpha, red, green, blue; the cursor and translucent artwork |

In the menus and a race, the game uses all four: 175, 380, 616 and 140
textures of each, in that order.

## Render states

`setstate(id, value)`. The meanings come from d3da's mapping to
Direct3D:

| Id | Meaning |
|---|---|
| 0x1 | texture handle |
| 0x2 | cull: 0 none, 1 counter-clockwise, 2 clockwise |
| 0x3 | clear colour, 0xAARRGGBB |
| 0x4 | z test and write; the game sends 0 with softtria (and under nfs-host, whose `voodoo2a.dll` is softtria: `e1-tl1`'s frame 2818 sends it 0 four times and never 1, so the game draws by depth order, col.md, Over the cars) |
| 0x5 | dithering: not 0 `grDitherMode(2)` (4x4), 0 `grDitherMode(0)` (off); `voodoo2a.dll` sends itself 1 when it sets the video mode, the game never sends it (see Dithering) |
| 0x6 | shading: 0 flat (the first vertex's colour), 1 Gouraud |
| 0x7 | texture filtering |
| 0x9 | antialiasing |
| 0xa | alpha blending: 0 off, 2 on |
| 0xd | texture addressing: 1 wrap, 0 clamp |
| 0xe | fog: 5 × (100 − density), 0 for none (see Fog) |
| 0xf | fog colour, 0xAARRGGBB |
| 0x13 | eight dwords, copied |
| 0x15 | fog mode: 0 off; 1, 2, 4 and 8 on (the game sends 4) |
| 0x16-0x18 | fog values; voodoo2a ignores 0x16 and 0x17 |
| 0x24 | alpha test, greater than 0 (the colour key) |
| 0x68 | blend: 0 source alpha / inverse source alpha, 1 source alpha / one, 2 zero / inverse source alpha, 3 destination colour / zero |
| 0x6a | z write |
| 0x190-0x195 | assorted; 0x193 returns capabilities, 0x194 and 0x195 set source and destination blend from tables |
| 0x2bd, 0x2bf, 0x2c2 | softtria's own; d3da ignores them and returns 0 |

## Blend modes

State 0x68 picks the blend; the game sends only 0 and 1 (the polygon
drawers `0x4332b0`, `0x433530`, `0x433660`, `0x4339d0`, by bit 0 of a
polygon's flags) and state 0xa once, from the renderer set-up
(`0x4b59c0`). On a Voodoo, 1 is Glide's (source alpha, one), so an
opaque added pixel is added in full.

**softtria** (read in November 2026 from `softtria.dll`, at its own
base 0x60000000; its `setstate` is `0x600030c0`):

- It ignores state 0xa (`0x6000324f`-`0x6000325d`: 0xa and 0xb return
  1 and store nothing), and takes only 0 and 1 for 0x68
  (`0x600033bf` calls `0x60004190`, `0x60004280` and `0x600043c0`, each
  of which does nothing for other values).
- Translucency is per triangle, not per pixel: `drawtri`
  (`0x60004da0`) takes the mean of the three vertices' alpha, m, and
  calls entry m >> 5 of a row of eight span drawers (`0x6003e12c`:
  `0x60041040` + 0x180 × state 0x2c2 + 0x20 × the texture's kind, the
  kind being its format, 0 with no texture, and 3 or 4 for a 16-bit one
  by whether it has see-through texels, `0x60003dc0`), with 16 levels,
  m >> 4, for the drawer (`0x6003e124`, `0x6003e128`). Entry 0 is a
  bare `ret` (`0x6002fcb0`) in every row: a triangle whose vertices
  average under 32 alpha isn't drawn at all.
- State 0x68 changes the rows of the textured kinds 3, 4 and 7. Under 1,
  entry 7 (m from 224) of kind 4 is `0x60026700`, whose span
  (`0x60026610`) adds the texel to the pixel, each 5:6:5 field clamped
  (the masks at `0x6003e33c`-`0x6003e344`), with no alpha and no vertex
  colour. So softtria adds an opaque pixel in full too.

**Reluva** (November 2026): the GPU drawing (`thrash_gpu.cpp`) blends
with (source alpha, one) for mode 1 while state 0xa is on, so it added
opaque pixels; the software rasterizer (`NativeThrash::put`) applied a
blend only to pixels under alpha 255, so it replaced an opaque added
pixel instead of adding it, and mode 2 and 3 likewise. Now it applies
0x68's mode whenever 0xa is on, opaque or not, and mixes by alpha
otherwise, as the GPU does (test `testThrashBlend`). Neither follows
softtria's per-triangle alpha levels, its untextured rows or its
additive spans' ignoring the vertex colour: Reluva draws as the 3D
cards do.

## d3da's device defaults

`d3da.dll` sets up its Direct3D device once (`0x60004137`-`0x600042af`,
IDirect3DDevice2::SetRenderState through vtable `+0x5c`, constant
arguments; November 2026): colour keying off, alpha blending on with
source alpha over, alpha test on (greater than 0), z test and write on
(less or equal), perspective-correct texturing, TEXTUREMAG and
TEXTUREMIN LINEAR (bilinear), texture blend MODULATEALPHA, clamped
addressing, Gouraud shading, solid fill, dithering off, specular off,
antialiasing off, fog off (colour 0x010101, table mode linear, 0 to 1).
Its `setstate` (`0x60002b58`, the wrapper `0x60004f64` takes the state
in eax and the value in edx) changes the filter only for state 7 and
never the texture blend. So under Direct3D the game's textures are
bilinear-filtered and undithered, where the Voodoo's are point-sampled
and dithered (below). What else the game does differently for Direct3D
is in [hrz.md](hrz.md), The Direct3D renderer.

Its `setstate` (read in November 2026 from the jump at `0x60002b6b`):
state 5 does nothing and returns 1, so the device never dithers; state 7
sets TEXTUREMAG and TEXTUREMIN to NEAREST for 0 (`0x600030de`) and
LINEAR for 1 (`0x60003101`) and refuses other values; state 6, 0 sets
flat shading. Its `about()` (`0x60001020`) gives the signature
0x44334435 ("5D3D"), version 0x68, texture sizes 8 to 256 (softtria 16
to 256), a texture format table (`0x60010e44`) and the texture memory
(+0x70, +0x74) that it fills from the Direct3D device it finds.

**The Direct3D look** (`nfs-host -direct3d`, the Play page's
`renderer=direct3d`, docs/engines/nfs.md): the game is told its
renderer is `d3da.dll` and softtria is loaded in its place, as for
`voodoo2a.dll`. The native drawing then follows the device above
(`NativeThrash::setDirect3D`): bilinear filtering (Direct3D's, texel
centres at half-integers, alpha filtered too, then the alpha test), state
7 switching between point and bilinear, no dithering whatever state 5
says; on the GPU each batch carries its filter
(`ThrashFrame::Batch::filter`). The texture blend (texel times vertex
colour, alpha too) and the blend modes are the same for both. The game
sends no fog to d3da, so none is drawn. The game still reads softtria's
`about()`: of its texture formats (2, 3, 4 and 7) the game uses 3, 4
and 7 in a race (1555, 565 and 4444, which Direct3D cards commonly
offered; what d3da would list depends on the card), and its flag at +0x75 has the game load the in-car dashboard, as
d3da's does with more than 4 MB of texture memory (`0x488a80`).

## Texture filtering

What the game asks for (November 2026): state 0x7 is the texture
filter, 0x8 the LOD bias and 0xb mipmapping. `voodoo2a.dll`'s `setstate`
turns 0x7 into `grTexFilterMode(0, v, v)`, 0x8 into `grTexLodBiasValue`
and 0xb into `grTexMipMapMode` (its jump at `0x600027a3`-`0x60002bd4`,
the Glide entry points it looks up by name at `0x60001272`-`0x60001579`
and keeps at `0x60011470`, `0x600114ec`, `0x600114bc`), and calls those
three nowhere else. nfs3.exe sends none of them to it: of the calls
through its pointer to `setstate` (`0x9ef96c`) only two pass 7, both in
the text drawing `0x4d7160` (`0x4d7252` point-sampled for a scaled
string, `0x4d7547` bilinear after it) and only with the Direct3D
renderer (`0x7a3a58` bit 0x40); the renderer set-up `0x4b59c0` sends
voodoo2a states 10, 4 and 2, and the wrapper `0x431900`'s callers pass
other states. A race's list confirms it (`d6-tlog`, Hometown by night,
`-thrashlog` of frame 2345: states 1, 2, 3, 4, 0xd, 0xe, 0xf, 0x15 and
0x68 only).

**But the DLL sends them to itself** (November 2026, found running it on
the Glide stand-in, docs/common/graphics/glide.md). After `setvideomode` it runs
its own list of `setstate` calls (`0x6000207d`-`0x6000211f`, the same run
that turns on dithering, Dithering below): 1 0 (no texture), 2 1, **7 1**
(`0x6000208f`: `grTexFilterMode(0, GR_TEXTUREFILTER_BILINEAR,
GR_TEXTUREFILTER_BILINEAR)`), 6 1, 0xa 2, **0xb 1** (`grTexMipMapMode(0,
GR_MIPMAP_NEAREST, FXFALSE)`), 3 0, 0xc 0, 5 1, 0xe 0, 0xf −1, 0x68 0, 0xd
0, 0x65 1.0 (`grGammaCorrectionValue(1.0)`), 0x18 0, 8 0 (LOD bias 0) and,
with a depth buffer, 4 2. So with `voodoo2a.dll` the game's textures are
**bilinear-filtered**, for minification and magnification; its textures
have one level each (`talloc` asks for none smaller), so the mipmap mode
changes nothing. An earlier reading here had them point-sampled, from
the default filter Glide's own sources set; that source is not used any
more, and the DLL's own set-up overrides any default anyway. Since
November 2026 the native THRASH (`thrash_native.cpp`, the GPU's drawing
and the software rasterizer) and the Track and Cars pages filter
bilinearly too by default; `texturefilter=point` in reluva.ini (or
`nfs-host -pointfilter`) holds them point-sampled. In the Direct3D look
d3da's state 7 still picks. Their bilinear filter is Direct3D's (texel
centres at half-integers, float weights); the Voodoo's own, with its
fixed-point weights, is the Glide stand-in's (`renderer=glide`). The
pages filter the track's pictures, the sky's, the cars' skins and reflection maps and
the officer's pages (`glideTextureParams`), and since November 2026 the
glows, beams, flares, particles and the headlights' picture too: in the
lists they are textured batches like the others (`d7-night1`'s frame
2700: the cars' glows on `glw3`'s 32 × 32, mode 1, `e5ffffff` and
`66ff0000`, their u, v 1/64 to 63/64; the fog's beams on `glw1`'s 32 ×
32, `72ffffff`; `d5-fog`: the rain's flares on `glw4`'s 64 × 64), with
no texture state of their own.

## Fog

The game fogs only with a 3dfx renderer. Its set-up for each renderer
(0x4b59c0, a jump table by renderer number) sets the fog mode it sends
in state 0x15 (0x7a3a64): 4 for `voodooa` and `voodoo2a`, 0 for the
rest. With 0 it never sends state 0xe or 0xf either (0x470aa0,
0x470ad0). So d3da draws no fog, and softtria gets none to draw.

What the game sends (0x470ad0): state 0xe =
5 × (100 − density) for the track's fog density in percent (its `.hrz`
file and fog regions, [hrz.md](hrz.md)), clamped to 5..500, or 0 for
none. State 0xf is the fog colour.

What `voodoo2a.dll` does with them (its `setstate`, disassembled):

- **0xe, value v.** 0 turns fog off (`grFogMode(0)`). Otherwise it turns
  on table fog (`grFogMode(2)`) and gives Glide a table of 64 entries
  (`grFogTable`). Entry i is for the distance
  w(i) = 2^(3 + i / 4) / (8 − i mod 4) (i / 4 rounded down), Glide's own
  spacing: 1, 8/7, 8/6 and 8/5 times each power of 2, up to w(63) =
  52428.8. The entry is

  f(w) = (1 − e^(−w / v)) / (1 − e^(−w(63) / v)),

  clamped to 0..1, times 255 and rounded.
- **0x15, mode m.** 0 turns fog off; 1, 2, 4 and 8 turn table fog on
  with the table it has.
- **0xf.** The colour, to `grFogColorValue`.

Glide fogs each pixel by its w, from the vertex's 1/w (rhw), reading the
table between the two entries either side. It mixes the pixel's colour
towards the fog colour by the entry over 255, before blending.

Reluva draws the same from the same states, with any renderer DLL
loaded:
- **Natively** (`NativeThrash`): the same 64 entries, read as Glide
  reads them, for each pixel's w.
- **On the GPU**: the curve f(w) itself, worked out in the fragment
  shader for each pixel's w (`DrawCall::tableFog`).

Clears aren't fogged. Each batch on the GPU carries the fog it was
drawn with.

## Dithering

`voodoo2a.dll` draws into a 16-bit (5:6:5) buffer with Glide's 4x4
ordered dither on (November 2026, read from the DLL with `pefile` and
`capstone`). Its `setstate` (`0x60002790`) takes id 5 to the
`grDitherMode` it looked up at start (`0x60011474`, from the name
`_grDitherMode@4` at `0x6000f1f4`): `push 2` for a value not 0, else 0
(`0x60002a94`-`0x60002ab4`, `0x60002b95`). Its own set-up of the states
after `setvideomode` (`0x60002081`-`0x6000211f`, a run of
`setstate(id, value)` calls) sends `setstate(5, 1)`, and the game never
sends id 5: a `-thrashlog` of a whole race on Empire City (`e5-tl`,
2,920 frames, 862,335 `setstate`s) has none, and of the 167 calls to
the game's `setstate` wrapper (`0x431900`, the id in eax) none loads a
5 (one takes its id from a record, `0x43485d`; that run didn't send 5
either).

`nfs-host`'s native drawing (`thrash_native.cpp`) had truncated each
pixel to 5:6:5. A blend reads the pixel back from 5:6:5, so where
translucent quads overlap, each layer lost up to a step (8 of 255 in
red and blue) and with a low alpha the loss holds the colour back: the
tyre smoke ([col.md](col.md), eight quads at alpha 62-100 × the picture's
up to 217) came out 9.5-10.7 darker than on the Track page where the
scenery round it was 1.5-4 darker. It now dithers as Glide is asked to
(state 5, on from the start): each channel scaled to its bits plus the
4x4 Bayer matrix's threshold at the pixel, (2k + 1) / 32 of a step,
then truncated, so a region keeps its colour on average. The matrix is
Glide's documented 4x4 one; the Voodoo's own rounding of it isn't known
from the code. Measured on Empire City's frame 2816 (`e5-t8f0` before,
`e5-t8d0` after, the page from the dumped camera, cars and particles):
the smoke 2.8-4.1 under the page, the scenery round it 0.8-2.4 under;
over the whole frame the host's mean rose by 2.1, 1.1, 3.1 in red,
green and blue. Since the hashed end state of `-deterministic` runs
holds the frame buffer, a `-native` run's hash differs from an older
build's from the first frame drawn; the game's own state doesn't.

**What was left of the smoke's difference** (November 2026, `e6-runs`:
Empire City by day, Hot Pursuit poked at `0x474ca0` (`0x6fd3b8` 3, track
8), racer 1 held 18 m ahead with `d8-runs/t8-car1.txt`'s pokes, the
camera, cars and particles dumped at the 400th sky draw, frame 2816 and
list 2818; `t8d`). The smoke's batch is the frame's last but the
cockpit's (8 quads, `SP06` at 32 × 32, alpha blend, fog 475 like the
scenery, corners `48e3e3e3`-`6ef6f6f6`); the page draws the same quads
after the cars, fogged as the scenery, `SrcAlpha`/`OneMinusSrcAlpha`,
with the same colours. Over the smoke the page was 3.1-4.4 above the
host's frame, the road beside it 0.2-1.3. Two causes:
- **The picture.** `SP06` is a 32-bit shape; the game hands it to the
  card at 4:4:4:4, each channel's top four bits (`0x4d37a0` →
  `0x4d27e0`, below, Textures as loaded), so its texels are multiples of
  17 in the list. The page had taken it at full precision. Now that it
  takes it as the game does (`decodeForGlide`) its texels equal the
  list's (all 1,024), and the smoke is 2.3-3.6 over the host's.
- **The blend into 5:6:5.** `nfs-host`'s drawing truncates three times a
  layer: the shaded, fogged colour and its alpha to whole levels, the
  blend's `/ 255`, and the read back of the pixel (`c × 255 / 31`). A
  build with each rounded to the nearest instead (a scratch copy of
  `thrash_native.cpp`, not kept: `t8r`) puts the smoke within 0.0-1.0 of
  the page's and the road beside it within -0.5-1.1, as close as the
  rest of the frame. How a Voodoo 2 rounds its blend and reads back a
  dithered pixel isn't in the game's code or the DLL's, so the host keeps
  its truncation; that is what is left (2-3.5 over the smoke).

**What 3dfx's documents say** (November 2026). The Voodoo2 Graphics
specification (revision 1.16, December 1999, section 5.20 `fbzMode`)
and Glide 2.4's Programming Guide (Dithering) and Reference Manual
(`grDitherMode`) were read for the dither's arithmetic. They give:
`fbzMode` bit 8 turns dithering on, bit 11 picks the 4x4 (0) or 2x2
(1) ordered matrix, and with it off the 24-bit colour is truncated to
5:6:5; bit 19 subtracts the dither matrix from the destination colour
before an alpha blend ("dither subtraction"), and `fbiInit2` bit 0 can
turn off a "video dither subtraction"; Glide's default is
`GR_DITHER_4x4`. The fog unit's figure gives its own 4-bit dither index
from the pixel (bit 3 y0 xor x0, bit 2 y0, bit 1 y1 xor x1, bit 0 y1),
and textures are widened to 8 bits by repeating their top bits
({r[4:0], r[4:2]} for 5:6:5). They don't give the colour dither's
matrix values, how a matrix value is added to a channel and rounded,
whether Glide's `grDitherMode` sets bit 19, or how a 5:6:5 destination
pixel is widened for a blend. So the host's dither stays as it was
(the matrix's thresholds, truncated) and the rounding stays open. The
smoke and shadow-strip numbers above changed for another reason: on
Empire City's officer frame (`t8o`) the strip under the held car is now
+3.9, +4.3, +5.9 over the host's frame (was +6.4, +6.9, +8.5), because
the page's smoke over it had its picture mirrored ([col.md](col.md),
Its spread over the car); over the puffs themselves the page is +2.8,
+3.4, +3.8.

**Textures as loaded** (`0x4d4080` → `0x4d3f50` → `0x4d37a0`, `gdecomp`
and `asm`, November 2026). A shape is converted by its type and the
format asked for: a 32-bit shape (0x7D) asked for 4:4:4:4 (format 7: sfx's
`glw0`-`glw4`, `0x4d5370`), or asked for none with an alpha other than 0
and 255 (`0x4d1950` returns 2: `shad`, the particles' `SPnn`, `SNnn`),
goes through `0x4d27e0`: a = A >> 4, r = R >> 4, g = G >> 4, b = B >> 4
(the key colours `0xff00f800` and `0xff00fb00` become 0; with `0x7a3a6c`
set, only for renderer 5, a black texel with alpha gets blue 1). The
card widens each nibble back by 17. In `e6-runs/t8d`'s list `shad`,
`SP06` and `glw3` as the page now decodes them equal the list's textures
texel for texel.

## Windows and frames

- `window(n)` picks where drawing goes: 1 the front buffer, 2 the back.
- `lockwindow()` returns a structure:

  | Offset | Meaning |
  |---|---|
  | 0 | buffer address |
  | 4 | pitch in bytes (0x500) |
  | 12 | width (640) |
  | 16 | height (480) |

  softtria's buffers are 16-bit 5:6:5.
- `clip(x0, y0, x1, y1)` sets the clip rectangle.
- `clearwindow` clears the current window.
- `flushwindow` and `pageflip` end a frame. softtria's `pageflip` copies
  its buffer to the DirectDraw surface.
- `about()` returns a pointer to a structure describing the renderer.

## Page flips and clears, natively

With the retail `softtria.dll` (timestamp 0x35e4344f, image base
0x60000000), `nfs-host -native` does `pageflip` and `clearwindow` itself.
Both work on softtria's current window over its clip rectangle, which it
keeps in globals. Their addresses come from disassembling the two
functions:

| Global | Meaning |
|---|---|
| `0x6003e0fc` | the current window's buffer |
| `0x6003e0ec` | its pitch, in bytes |
| `0x6003e0b8`, `0x6003e0bc` | the clip rectangle's left (rounded down to 16 pixels) and top |
| `0x6003e0c0`, `0x6003e0c4` | its width (in whole 16-pixel columns) and height |
| `0x6003e11c` | the clear colour, as two 5:6:5 pixels |
| `0x600400b4` | a function of the game's, called with 1 before a page flip's copy and 0 after |

- `clearwindow` fills the rectangle with the clear colour.
- `pageflip` locks the primary DirectDraw surface and copies the
  rectangle into it, eight bytes at a time through FILD/FISTP. Then it
  unlocks the surface. It doesn't swap windows. Done natively, the
  rectangle goes straight to the frame shown, between the two calls to
  the game's function.

With any other renderer DLL, both are left to the DLL.

## Texture uploads, natively

With native drawing, `tupdate` doesn't call the DLL either (with any
renderer DLL). Its drawing is the only thing that reads its copy of the
textures, and it isn't called. The native copy, decoded to RGBA, is the
only one kept. The result is what softtria's `tupdate` returns: the
handle, or 0 for formats 3 and 4 given no pixels. `talloc` and `treset`
still go to the DLL, so the handles stay its own; they cost about a
million instructions in a run.

In a run into a race this takes 168 million instructions out of the
DLL, about 3% of the run. Loading hardly changes, though. The longest
waits between frames, at start-up and while the track loads, were 5.3
and 1.4 seconds, against 5.4 and 1.3 with the DLL's uploads. Those
waits are spent elsewhere.

## On the GPU

With a GPU, the Play page draws THRASH's frames with it
(`src/engines/nfs/thrash_gpu.cpp`). `GameHost` (with `gpuThrash`)
records each frame's drawing as a list, and the page draws the lists.

- **Triangles.** Each triangle gets its three vertices of its own, so
  flat shading is the first vertex's colour on all three. Quads are two
  triangles. Lines are quads a pixel wide, and points one-pixel squares.
- **Batches.** A new batch starts when the texture, wrapping, blending,
  culling, fog or clip rectangle changes. Clears are filled rectangles in the
  list, in their place.
- **Textures.** Those made or changed since the last frame go with the
  list, decoded to RGBA as for the software rasterizer.
- **Vertices** go to the GPU as (x / rhw, y / rhw, 1 / rhw). A matrix maps
  that to clip space, with clip w equal to 1 / rhw, so texturing is
  perspective-correct as in the software renderer. A triangle whose three
  vertices share one rhw is flat on the screen, and goes with w = 1.
  2D pictures come with rhw = 1/65536, which would make w huge for
  nothing.
- **Clipping.** The batch's clip rectangle becomes the viewport, with the
  matrix fitted to it.
- **No depth test.** The game sorts back to front for the software
  renderer.
- **Blending.** Translucent texels and vertices blend over what's drawn,
  with source alpha. State 0x68's other modes apply while blending is on
  (state 0xa). Texels of alpha 0 are left out.
- **One pipeline for the blends.** Nothing reads the frame's alpha, so
  the page says so (`Renderer3D::setFrameAlphaUnused`) and the renderer
  draws blended and added batches with one pipeline state, the factors
  applied in the fragment shader. A race alternates the two about 1,500
  times a frame (of some 2,000 batches); each pipeline switch took SDL
  four new descriptor sets.
- **Culling.** Cull 1 (Direct3D's counter-clockwise culling, in d3da)
  drops triangles turning counter-clockwise as shown on the screen. Cull
  2 drops clockwise ones.
- **Frames add to the last.** softtria's buffers keep their contents
  from one frame to the next, and the game sometimes draws only what's
  new: a loading screen draws its picture once, then a step of its
  progress bar each frame. So the GPU's frame keeps its contents too
  (`Renderer3D::setKeepFrame`), and lists are drawn in order, each with
  the texture changes it brought. The page draws all the lists waiting
  into one frame, shown once. A list of 1,000 vertices or more is taken
  to draw the whole screen (menus have 2,000 to 3,000, races tens of
  thousands), so those before the last such list are skipped, their
  texture changes still applied. At most about 150,000 vertices wait.
  With more, the oldest go, as when the page isn't shown.
- **The flip's rectangle.** A page flip copies only softtria's clip
  rectangle to the screen. DirectDraw's last picture stays outside it
  until a flip covers the whole screen.

Checked first that nothing is lost by not drawing into softtria's
buffers. With drawing turned off and clears made magenta, every frame
from the menus, through the track's loading screen and into a race was
magenta throughout. So the game draws only through THRASH.

**Compared with the software rasterizer.** With `RELUVA_NFS_COMPARE` set
to a folder, the Play page has the game drawn both ways, at 640x480
(point-sampled when the runs below were made; both now filter as
`texturefilter` says). Every 60th frame it writes the GPU's frame and the
software one of the same page flip. In a run through the menus, a
loading screen and a race in three camera views, under software Vulkan:

| | Mean difference (of 255) | Pixels off by more than 24 |
|---|---|---|
| Menus | 0.25-0.46 | 120-150 |
| Loading screen | 0.01-0.03 | 0 |
| Race | about 3 | about 200 |

The race's difference is mostly the software path's 5:6:5 colour.
Rounded to it, the GPU's frame is within 1.1 on average; the rest is
pixels along edges. The comparison found two bugs, both fixed:
- **Culling.** The mapping had cull 1 and 2 the wrong way round, which
  left the loading screens and the countdown's digits out.
- **Skipped lists.** The page drew only the latest list, which lost
  frames that add to the last.

`nfs-host -gpulog` records as for the GPU, without one, and sums up
each frame's list: batches, vertices, textures, the flip's rectangle. `nfs-host -gpudump n dir` writes the nth list out in full: every
vertex with its batch's texture, blending, culling, fog and texture
filter (`list.csv`; the `filter` column is −1 where the batch leaves it
to the drawing's default, 0 point-sampled, 1 bilinear: set only under
d3da's device, from state 7), and the textures it uses as TGA files. That is how the
game's sky was measured ([hrz.md](hrz.md)).

**Memory mappings** (November 2026). A second race in one session
crashed under Mesa's software Vulkan driver: the process had reached the
kernel's limit of 65,530 memory mappings, one per Vulkan descriptor set
there. SDL's Vulkan backend keeps a cache of descriptor sets for every
command buffer in flight and never frees one (sdl_gpu.h, copyPass), and
every texture the page made had brought a command buffer of its own, as
did SDL's making of it. With the uploads already sharing one, the front
end alone still made 117 caches, more of them growing to a race frame's
draws with each race. Now uploads share a
command buffer, at most two frames are in flight, the renderer waits for
the GPU after every eighth texture (11 caches) and the blends share a
pipeline. Mappings (`/proc/<pid>/maps`, the most in each stretch), Play
page, Hometown, quitting to the menu between races:

| | menus | 1st race | 2nd race | 3rd race | 4th race |
|---|---|---|---|---|---|
| Before (Direct3D look) | 10,400 | 44,300 | crashed loading | | |
| After (Direct3D look) | 6,600 | 19,000 | 23,800 | 28,300 | |
| After (default) | 5,600 | 17,700 | 24,800 | 28,700 | 32,400 |

Each race adds less, as the caches reach the largest frame's size. The
frame rate is unchanged: about 8 frames a second in a race under this
software driver, as before. The Track page with computer cars and rain
stays at 7,000-7,700 over seven minutes.

DirectDraw's frames (the movies) are shown over an empty frame. Frames
are drawn at `renderscale` times 640x480 (2 unless set): the same
picture, with sharper edges. Textures are bilinear-filtered, as both of
the game's renderer DLLs ask for, or point-sampled with
`texturefilter=point`; in the Direct3D look
(`renderer=direct3d`) as d3da's device filters them, whatever that says. Both settings are in the game's
section of `reluva.ini`. Without a GPU, the software rasterizer draws
as before.

## The Voodoo3 patch's voodoo2a.dll

The Classic CD has a `Patch/Voodoo3` folder with a newer
`voodoo2a.dll` (110,592 bytes, PE time stamp 1999-04-21), a
`3ddata.dat` and `3DDATA.EXE`, and a readme. The readme says to run
`3DDATA.EXE`, copy `3ddata.dat` into the install's `3dSetup` folder,
copy the DLL over the install's, and then pick "3dfx Voodoo3" in 3D
Device Setup. The install's `voodoo2a.dll` is the retail one (93,382
bytes, 1998-07-31). This section was read from both files (November
2026, `pefile` and `capstone`; scripts kept outside the repo). The
CD's `nfs3.exe` is byte-identical to the install's (1998-09-03). The
patch DLL loads Glide 3 (`glide3x.dll`) by name. Since November 2026 it
runs under nfs-host on Reluva's Glide 3 stand-in (`-glidedll`, below,
Running the patch DLL); the rest of this section was read from the code
and, where marked, checked by running it.

**The build.** The retail DLL was built with Watcom (sections `AUTO`,
`DGROUP`) and the patch with Microsoft's compiler (`.text`, `.rdata`,
`.data`). The retail one runs on Glide 2 (`glide2x.dll`: it uses
`grSstQueryHardware`, `grSstWinClose@0`, `grTexDownloadTable@12`,
`grLfbWriteRegion@32`, `grHints`, `grSstControl` and so on). The patch
runs on Glide 3 (`grVertexLayout`, `grCoordinateSpace`, `grGet`,
`grGetString`, `grEnable`/`grDisable`, `grErrorSetCallback`,
`grQueryResolutions`, `grSstWinClose@4`, `grTexDownloadTable@8`,
`grLfbWriteRegion@36`, `guGammaCorrectionRGB`). It looks all of them
up by name at `0x60001000`-`0x6000143e`, into `0x60018720`-
`0x600187f8`. `grTexDownloadMipMapLevelPartial` and
`grQueryResolutions` are looked up there but the drawing code doesn't
use them. The patch also tells the card apart by `grGetString`'s
hardware name (Voodoo Graphics, Voodoo Rush, Voodoo2, Voodoo Banshee,
Voodoo3, `0x60001730`), and has CPU checks named `ISMMX`, `ISK3D` and
`ISKNI`.

**Exports.** The retail DLL exports the 34 functions the game looks up.
The patch exports the same 34, with different ordinals, plus
`drawlinestrip@12`, `drawtrifan@12` and `drawtristrip@12`. `nfs3.exe`
never asks for those three: its loader `0x4f8390` looks up only the 34
`@8`/`@4`/... names, as with every renderer.

**`about()`'s fields** (November 2026, the patch's `0x60001560`; the
game's readers found with `tools/nfsre`'s `xref 0x9ef980`, the pointer
`0x4f8390` stores `about` in). The structure is 208 bytes at
`0x60017df8`, filled once:

| Offset | Patch DLL | What `nfs3.exe` does with it |
|---|---|---|
| +0 | "2FD3" (retail "XFD3") | `0x4f8390`: "XFD3" sets `0x564410`, which nothing reads |
| +4 | 0xd0, the structure's size | — |
| +8 | the version (0x68-0x6a, negotiated) | the version check above |
| +0xc | flags: 1, 2, 4, 8, 0x40; 0x80 when one TMU | — (not read by the callers found) |
| +0x10-+0x20 | 1, 0x100, 1, 1, 0x100 | the cockpit loader (`0x488afc`-`0x488b2a`): +0x10 ≤ 0x100, +0x14 ≥ 0x100, +0x1c ≤ 0x100, +0x20 ≥ 0x100, else no cockpit: the smallest and largest texture sides |
| +0x24, +0x28 | 1, 1 | — |
| +0x2c, +0x30 | 11 and a table of 11 texture formats (`0x60014068`) | `0x49c5c0`: picks texture type 3 if the table's first entry has flag 1 at +0xc, else 7 if the second has it at +0x1c, else "Cannot support texture type" |
| +0x34, +0x38 | 4 and a table (`0x6001409c`) | — |
| +0x3c, +0x40 | 19 modes and their 40-byte records (`0x600140b8`) | `0x4be570`, `0x4bed90`, `0x4bee80`, `0x4bef50`: the mode number checked against +0x3c and its record copied (10 words to `0x7cdae0`) |
| +0x44-+0x50 | three words and a half-word from `0x600115b0` | — |
| +0x6c | the board, 0-4 by `grGetString(GR_HARDWARE)` | — (not read by the callers found) |
| +0x70 | the texture budget (Memory, below) | `0x488a80` (the cockpit) and `0x4b5ea0`: compared with 2 MB and 4 MB |
| +0x74 | 3 (Glide 3), the retail DLL and `voodooa.dll` 1 | `0x488ad3`, `0x4b5ebe`: added to +0x70 before a comparison with 4 MB (one caller) or 2 MB (mode 6 only); bit 0 tested (`0x488ae0`); bit 15 tested (`0x488ac5`), set by none of the three DLLs |
| +0x78 | `0x60014644` | — |
| +0x7c | 5 | — |
| +0x80 | the hardware name (version 0x6a only) | — |

The cockpit loader (`0x488a80`), when the game's flags at `0x7a3a58` ask
for the check: with bit 0 of +0x74 set (every DLL here) and +0x70 at or
below 2 MB the cockpit is left out; above 2 MB (and below 4 MB plus +0x74)
the texture sides decide. That is why `voodooa.dll` on a 2 MB-texture
Voodoo Graphics races without the cockpit (The Glide stand-in, The Voodoo
Graphics module, below). Each field can be overridden by a
`VOODOO2_<name>`/`THRASH_<name>` variable (`0x60004ff0`).

**`about()` and the game's check.** The retail `about()` (`0x60001a60`)
fills its structure with the signature 0x33444658 ("XFD3" in memory)
at +0 and the version 0x68 at +8. The patch's (`0x60001560`) puts
0x33444632 ("2FD3") at +0 and 0x6a at +8 (from `0x60014648`). It also
lets environment variables override each field, `VOODOO2_<name>` and
then `THRASH_<name>` (`0x60004ff0`, `0x60004f80`: `signature`,
`version`, `linewidth`, `texturesquare`, `software`, `windowed`,
`globalclut`, `trilinear2pass`, the texture size limits and more).
The game's loader `0x4f8390` sets `0x564410` to 1 when the signature is
"XFD3" (nothing reads `0x564410`). When +8 is not 0x68 it logs "DRIVER
VERSION MISMATCH" (`0x4f876a`, through `0x4df6c0`) and returns 0. Its
caller `0x4f87e0` drops that result, and by then every function
pointer has been looked up. So this exe would go on with the patch DLL
after the message, from the code. **Run, the message never comes**: the
loader first sends `setstate(0x1f, 0x68)` (`0x4f8741`), the version it
wants, then calls `about()` (`0x4f874d`). The patch's `setstate` passes
ids it doesn't handle itself to `0x60005280`, whose case 0x1f
(`0x600053a2`) keeps the value as its version (`0x60014648`) when it is
0x68 to 0x6a and takes 0x6a otherwise. So `about()` reports 0x68 to this
game: watched at `0x4f8759` (nfs-host `-watch`), its structure holds
"2FD3" at +0 and 0x68 at +8, and `0x4f8769` (the message) is never
reached. The DLL negotiates the version; the earlier reading missed the
`setstate` before `about()`.

**Video modes.** Both take `setvideomode(mode, buffers, depth)` (the
game sends 1, 2, 1: 640x480, double-buffered, with a depth buffer).
Both pass the call to the window's thread (message 0x465 by
`PostMessageA`, then wait). The retail one (`0x60001f80`) first asks
`grSstWinOpen` for resolution 0xff (none, the board's own) and falls
back to its table at `0x600100e8`. The patch's (`0x60001f40`) maps the
mode straight to a Glide resolution through `0x60014400` (1 to 7,
640x480; 2 to 8, 800x600; and so on). Its mode records (40 bytes from
`0x600140b8`: width, height, 16, 4, a usable flag, two buffer counts)
number 19 against the retail's 16 (`0x6000fe18`). Modes 3-5 change
from 320x200, 320x240 and 400x256 (not usable) to 1024x768,
1280x1024 and 1600x1200 (usable). The retail's 1280x1200 (mode 14)
becomes 1280x1024, and 320x200 and 320x240 move to 17 and 18. Mode 1
(640x480) is the same in both.

**Vertices.** The retail `drawtri` (`0x60003570`) builds Glide 2
`GrVertex`es: x, y; 1/w × 256 (`0x60010158`); z × 65536
(`0x6001015c`); s and t × that 1/w; and the four colour bytes turned
into floats through a 256-entry table (`0x60011010`). The patch's
(`0x60003e30`) first culls by the sign of the screen area itself (the
cull state at `0x60017f14`). It then copies each 32-byte game vertex
with the same scaling, done on the float's exponent bits (z + 16 in
the exponent, 1/w + 8). The colour stays packed. Glide 3 is told that
layout once, at `setvideomode` (`0x60002075`-`0x6000211f`): window
coordinates, xy at 0, z at 8, q at 12, packed ARGB at 16, st0 at 24.
That is the game's own vertex layout ([Vertices](#vertices)).
`setvideomode` also turns on Glide's mipmap dithering (`grEnable(2)`)
and sets culling off (`grCullMode(0)`). Whether to draw with
`grDrawTriangle` or `grAADrawTriangle` is set at `0x60003840`. A
two-pass path there (`0x600038b0`, `0x60003940`) draws a triangle
twice, with fog off for the second pass.

**Textures.** The format numbers map to Glide's the same way in both
(`0x60010148` retail, `0x60014464` patch): 2 to `P_8` (5), 3 to
`ARGB_1555` (11), 4 to `RGB_565` (10), 7 to `ARGB_4444` (12), 8 to 4
and 9 to 14. The patch adds 12 to 1, which the game never sends. Both
place a texture in TMU 0 so that it doesn't cross a 2 MB boundary.
Both hand the texels to `grTexDownloadMipMap` as they are. When
`0x60014044` is 2 or more, the patch also loads every
texture into a second TMU, sets both as the source, and sets both
filters. That count isn't the board's: `0x60001a7b`-`0x60001ae2` asks
`grGet(GR_NUM_TMU)` but stores 1 in `0x60014044` before and after the
call, and only the environment variable `VOODOO2_NUMTMU` changes it. So
on any board the patch uses one TMU unless told otherwise (run: two TMUs
reported, and `grTexCombine` called once, for TMU 0 only, where
`setvideomode` would set TMU 1's too). The
palette changed. The retail `tupdate` (`0x60002430`) downloads a
palette to the board's one table (`grTexDownloadTable(0, 2, ...)`)
when it is given, so the last palette sent is used for every 8-bit
texture. The patch's (`0x600024c0`) keeps a 1 KB copy with the texture
when texels and palette come together. `settexture` (`0x600025a0`)
downloads that copy each time the texture is picked. A palette sent
alone still goes straight to the board.

**States.** The patch's `setstate` (`0x60002940`, a jump table for ids
1-0x6f at `0x600031b4`/`0x60003138`) changes these:

- **Defaults.** After `setvideomode` it sends itself a list of states
  (`0x60005440`), as the retail one does (`0x60002081`-`0x6000211f`).
  Each value can be overridden by an environment variable
  `VOODOO2_<name>` or `THRASH_<name>`: `CULL`, `FILTER`, `SHADE`,
  `TRANSPARENCY`, `ALPHATEST` (0x24, 0x10), `MIPMAP`,
  `BACKGROUNDCOLOUR`, `CHROMACOLOUR`, `DITHER` (5, 1), `FOGMODE`
  (0x15, 0), `FOGDENSITY` (0xe, 0), `FOGCOLOUR` (0xf, −1), `ALPHA`,
  `TEXTURECLAMP`, `DEPTHBIAS`, `DEPTHBUFFER` (4, 2 with a depth
  buffer), `MAXPENDING` (0x6d, the buffer count − 2), `BACKBUFFERTYPE`
  (0x2b), `FLATFANS` (0x11), `LINEWIDTH` (0x10), `DEFAULTPALETTE`
  (0x2c) and `STENCILBUFFER` (0x2f). It also sends 0x2e (1.0), 0x29,
  0x2a and 0x10029. Ids 0x24, 0x29-0x2f, 0x6d and above are new.
- **Dithering (5).** A value v other than 0 becomes
  `grDitherMode(v xor 3)`: 1 gives 2 (4x4), as in the retail DLL, and 2
  gives 1 (2x2). 0 turns dithering off. Its own default is 1, and the
  game doesn't send id 5.
- **Gamma (0x2e, 0x65).** The value is written as text into the
  `SST_*GAMMA` and `SSTV2_*GAMMA` environment variables. With a window
  open, it then goes to `guGammaCorrectionRGB` (`0x60002e35`).
- **Fog (0x15, 0xe).** The table is the retail one's
  ([Fog](#fog)). `0x60003280` works out (1 − e^(−w·d)) / (1 − e^(−w(63)·d))
  with d = 1/v, clamped and × 255, for Glide's 64 w values
  (`0x60001500`). It is now resampled to the board's own table size
  (`grGet(4)`, `0x60017de0`) when that is over 64, up to 1024
  (`0x600028a0`). 0x15 also changed. It now runs a jump table by value
  (`0x60003244`): 0 turns fog off (`grFogMode(0)`), 1 turns table fog
  on (`grFogMode(2)`), and 2, 4 and 8 only choose the table's shape
  (`0x60017dbc`). 2 is a curve made with 65536 (`0x60001470`), 8 is
  `0x60003340` and the rest are the curve above. 0xe no longer turns fog
  on; it only builds the table. The game sends 0x15 = 4 for this renderer
  (`0x4b59c0`, `0x7a3a64`). So, from the code, the patch DLL would
  draw this game with no fog unless `FOGMODE`/0x15 is 1. The retail
  DLL turns table fog on for 1, 2, 4 and 8. **Confirmed by running it**
  (below): in 3,829 frames through the menus into Hometown's race the
  DLL called `grFogMode` 4,950 times, every time with 0
  (`GR_FOG_DISABLE`), and `grFogTable` 4,943 times. The race is drawn
  without fog.

**`3ddata.dat` and `3DDATA.EXE`** (studied in [3ddata.md](3ddata.md)).
Neither reaches the game: `3DDATA.EXE` writes a card table (names, PCI
IDs, renderer modules) into the registry's "3D Data" key, now with the
Voodoo3 (module `voodoo2`), and `3ddata.dat` is 3D Device Setup's text
list of the cards the game supports and a few switches; the patch's adds
a "3Dfx Voodoo3" line sending that card to `voodoo2a.dll` and turns off
the offer of Direct3D-only cards. 3D Device Setup then writes the
choice's "Thrash Driver" for the game, which Reluva sets itself.

**Running the patch DLL** (November 2026). `reluva-tool nfs-host
<game> -glidedll <file>` loads a THRASH DLL file from outside the install
in `voodoo2a.dll`'s place (the CD's file, never copied into the repo);
one that names `glide3x.dll` gets Reluva's Glide 3 stand-in on a
Voodoo3 (docs/common/graphics/glide.md, The Glide 3 front end). The Play page
takes `glidedll=<file>` with `renderer=glide`. Its C library (Microsoft's)
also needed `CompareStringA`/`W`, `GetWindowThreadProcessId` (to post to
the window's thread) and `IsProcessorFeaturePresent` (looked up, absent,
as on Windows 95). What it does on Glide 3, from the run of the clicks
below (menus, Hometown's loading screen, the race; 5 billion
instructions, 3,829 frames) and the code:

- **Detection** (`0x600016a4`): `grGet(GR_NUM_BOARDS)`, `grGlideInit`,
  `grDisable(GR_SHAMELESS_PLUG)`, then `grGetString(GR_HARDWARE)`
  compared with "Voodoo Graphics", "Voodoo Rush", "Voodoo2", "Voodoo
  Banshee" and "Voodoo3 (tm)" (+0x6c of `about()`: 0 to 4), and
  `grGetString(GR_VERSION)` from its first digit against "3.10" (older:
  `grGlideShutdown` at every close). Reluva answers "Voodoo3 (tm)" and
  "3.10". `grGetString(GR_EXTENSION)` is searched for "PALETTE6666" and
  "TEXMIRROR" (`0x60001ae7`-`0x60001b46`). With PALETTE6666 the DLL loads
  its palettes as `GR_TEXTABLE_PALETTE_6666_EXT` (3, where 2 before:
  `0x60014058`, read at each `grTexDownloadTable`) and changes a word of
  its texture format table (`0x600140ac`, 5 to 1); with TEXMIRROR its
  `setstate` for the clamp mode may ask `grTexClampMode` for
  `GR_TEXTURECLAMP_MIRROR_EXT` (`0x60002dee`). Since November 2026 the
  stand-in's Voodoo3 offers both, as its specification has them (Boards).
- **Memory** (`0x6000198a`): `grGet(GR_NUM_FB)` (two or more: SLI),
  then `grGet(GR_MEMORY_UMA)`. If that succeeds and `0x60014030` is set
  (detection sets it to 1 every time, `0x600018c1`), +0x74 is 3 and the
  value read is the memory each mode's buffer count is worked out from
  (bytes ÷ width × height × bytes a pixel, `0x60001a0c`); else +0x74 is 1
  and `GR_MEMORY_FB` (doubled for SLI) is. Glide 3's grGet answers
  `GR_MEMORY_UMA` on every board (the total, or 0 when not UMA), so on
  Glide 3 this is always the first path, and a non-UMA board's 0 gives
  every mode 0 buffers there. For each usable mode the count is then
  replaced by `grQueryResolutions`' most colour buffers, asked with and
  without an auxiliary buffer (`0x60001b60`: resolution set, refresh and
  buffers `GR_QUERY_ANY`; 70 queries at start-up), and held to 3. So only
  the modes the DLL marks unusable keep the memory's count. The texture
  budget is `grTexMaxAddress − grTexMinAddress − grTexMinAddress`
  (`0x60001a7b`-`0x60001a96`), rounded up to 16: the same formula as the
  retail DLL's and `voodooa.dll`'s (`0x600019a7`).
- **Opening** (`setvideomode`, `0x60001f40`): `grSstWinOpen(hwnd, 7
  (640x480), 0, ARGB, upper left, 2, 1)`, keeping the context;
  `grGet(GR_VIEWPORT)`; `grTexCombine(0, LOCAL, ZERO, LOCAL, ZERO)`;
  `grEnable(GR_ALLOW_MIPMAP_DITHER)`; `grCoordinateSpace(GR_WINDOW_COORDS)`;
  the vertex layout (every parameter disabled, then XY at 0, Z at 8, Q at
  12, PARGB at 16, ST0 at 24); `grCullMode(0)`;
  `grGet(GR_FOG_TABLE_ENTRIES)` (64 here, so the resampling at
  `0x600028a0` isn't used); then its state defaults.
- **Drawing**: 4.87 million `grDrawTriangle`s with the game's vertices
  as they are; 515,015 `grTexSource`s; `grClipWindow` per
  `clip`; `grBufferClear`, `grBufferSwap` after polling
  `grGet(GR_PENDING_BUFFERSWAPS)`; `grFinish` for `sync`; two
  `grLfbLock`s. The modes it asked for: `grDepthBufferMode` z (6,692) and
  w (once), `grTexFilterMode` bilinear once (state 7 never changes it),
  `grTexMipMapMode` nearest, `grDitherMode` 4x4, `grTexClampMode` wrap
  and clamp, `grFogMode` only 0.
- **`about()`** is called 1,839 times (143 million instructions: each
  call reads the `VOODOO2_*`/`THRASH_*` environment again).
- **Frames**: the menus, the loading screen and the race draw as with the
  retail DLL, without fog (screenshots in the session's scratch folder,
  not in the repo). The run takes 256 s in software (Release build; the
  retail DLL's same run 215 s, both with other work on the machine).
- **On the unified-memory Voodoo3** (November 2026, after the stand-in's
  Voodoo3 became the specification's one 16 MB memory): the same run,
  3,829 frames, menus to the race with the cockpit, 248 s.
  `grTexMinAddress` is now 1,843,200 (above the three 640x480 buffers),
  so the budget (`about()` +0x70) is 16 MB − 8 − 2 × 1,843,200, 13,090,816
  bytes after rounding.
- **With the extensions and the 16-byte `grTexMaxAddress`** (November
  2026: the stand-in's Voodoo3 offers PALETTE6666 and TEXMIRROR, and
  `grTexMaxAddress` is 16 MB − 16, docs/common/graphics/glide.md): the same run,
  3,829 frames, menus to the race with the cockpit, 297 s; the budget
  13,090,800. The DLL's one `grTexDownloadTable` goes as the 6666 kind;
  `grTexClampMode` was asked for wrap 4,942 times and clamp 1,655, never
  mirror. The frames look as before (screenshots in the session's
  scratch folder).

## The Glide stand-in

Since November 2026 the retail `voodoo2a.dll` runs unchanged on Reluva's
stand-in for `glide2x.dll` (`src/graphics/glide/`, docs/common/graphics/glide.md):
`nfs-host -glide`, or the Play page with `renderer=glide`. The game is
told of `voodoo2a.dll` as with the native drawing, so it sends the same
THRASH calls; here the DLL turns them into Glide calls and the stand-in
draws them by the Voodoo's rules. It goes through the front end, the
movies, loading screens and races. The DLL calls 59 Glide functions
(glide.md, NFS III's voodoo2a.dll); in the menus and a race it draws
with `grDrawTriangle` (4.9 million in a run of 3,909 frames), `grDrawLine`
and `grBufferClear`, sets state with the combine, blend, depth, fog, cull
and clip functions, and locks the frame buffer for the movies.

**Movies.** The game writes each movie frame through `lockwindow` into
the window it picked with `window`, the front buffer for most, and flips
twice only: on a Voodoo what's in the front buffer is on the screen. The
host shows the front buffer again after any THRASH call that drew or
wrote into it (`Glide2::takeFrontChanged`), not only at `grBufferSwap`.

**Compared with the native drawing.** `nfs-host -glidecompare out.csv`
runs `-glide` and also hands every THRASH call to the native rasterizer
(`thrash_native.cpp`) drawing into a buffer of its own; each page flip
gives both frames of the same game state, compared per channel (the
native frame widened from 5:6:5 as the Glide one is, its top bits
repeated). Runs: `-deterministic 50000`, `-nomovie`, the clicks
`16000:click,24000:click,32000:move0/240,36000:click,70000:+up` (menus,
Hometown's loading screen, the race's start), 5 and 4.2 billion
instructions, the mean absolute difference over the frame, of 255:

| | as the DLL asks (bilinear) | the filter held point-sampled (`-glidefilter point`) |
|---|---|---|
| Menus (frames 0-1,900) | 0.93 | 0.46 |
| Loading screen (1,950-2,250) | 0.86 | 0.37 |
| Race (2,400 on) | 4.53 | 0.84 |

(The bilinear run's native frames were widened as ×255/31, which adds
up to 1 a channel; its numbers are about 0.3-0.5 high for that.)

The causes, from the code:
- **Bilinear filtering.** `voodoo2a.dll` asks for it (Texture filtering);
  the native drawing point-sampled when these runs were made (it filters
  by default since, with Direct3D's filter rather than the Voodoo's). It is most of the race's difference:
  the road, scenery and sky are magnified textures. Held point-sampled,
  the cockpit is within 0.01 and the road within 0.5 (race frame 2800,
  `j1-runs/fr6`).
- **Rounding.** The stand-in multiplies as the Voodoo2 specification's
  combine figures do, `(a × (b + 1)) >> 8`, and blends so; the native
  drawing computes in floats and truncates once. Translucent text in the
  menus differs by a dither step at some pixels. So does the sky's upper
  band, red 1.9-2.3 lower on average: there the clear colour
  (`0xff78b6ef`) is under additive clouds (blend 1, vertex alpha 0xb9,
  fogged) and a 256 × 256 glow quad (race frame 2440's `-thrashlog`). The
  stand-in clears through the dither (`grBufferClear`, as the
  specification lets a fill do), the comparison's native clear truncates,
  and the blends round differently; which of those makes the band isn't
  settled.
- **Fog between table entries.** The stand-in reads the fog table as the
  `fogMode` figure has the hardware read it, linearly in the w float's
  mantissa (inverted 1/w); the native drawing interpolates linearly in w.
  The factor differs by under 1 of 255.
- **Edges.** The stand-in snaps vertices to 1/16 pixel and fills by
  Glide's rule; the native drawing takes pixel centres in floats. Single
  pixels along edges differ.

**Compared with softtria.** softtria can't be run beside it (one
renderer DLL at a time), and its frames come at other moments of the
game's clock; earlier the native drawing was measured within 1.7 of 255
of softtria's on a menu screen (How it was worked out), and it is within
0.46-0.93 of the stand-in's.

Speed (Release build): the stand-in's rasterizer takes about 33 ns a
pixel for a full-screen textured, fogged triangle. The run above without
comparing (`-glide`, 5 billion instructions, 4,017 frames through the
menus into the race) takes 258 s, about 15.6 frames a second; the DLL's
own code is 0.15 million instructions of a race frame's 1.4 million. The
Play page shows the menus at about 15 frames a second. The native
drawing is faster; the stand-in is a reference, not the default.

**The Voodoo Graphics module** (November 2026). The install's
`voodooa.dll` (93 KB, Watcom, the module for a Voodoo Graphics card) runs
on the Glide 2 stand-in on a Voodoo Graphics board: `nfs-host <game>
-glidedll <install>/voodooa.dll -glideboard voodoo1` (glide.md, NFS III's
voodooa.dll, and Boards). It passes x and y snapped and not unsnapped
(`+786,432.0`, `0x60003610`), which the stand-in reads as the SST-1's
12.4 registers would; it uses `GR_CMP_GEQUAL` for the alpha test where
`voodoo2a.dll` uses `GR_CMP_GREATER`; its board code treats a Voodoo Rush
and the SST-1's Pixelfx revisions apart (`0x600018c0`). The same run as
above (5 billion instructions, 3,942 frames, software, 221 s): menus,
loading screen and race. With the board's 2 MB of texture memory the
texture budget is under 2 MB and the game races without the cockpit
(`about()`'s +0x70, `0x488a80`); with `voodoo1,tmu=4` (the 6 MB card,
264 s) the cockpit is there and its frames against `voodoo2a.dll`'s on the
Voodoo2:

| | mean difference (of 255) | pixels differing | off by more than 24 |
|---|---|---|---|
| Menus (frames 200-1,600, every 200th) | 0.014-0.028 | 830-1,620 | 0-3 |
| Loading screen (1,800-2,200) | 0-0.001 | 0-46 | 0 |
| Race (2,400-3,000) | 0.13-0.37 | 5,100-22,800 | 400-820 |
| Race (3,200-3,800) | 6.4-12.2 | 73,000-122,000 | 30,000-43,500 |

The menus' pixels lie along the alpha-tested title letters (the alpha
test). The race's grow because the two runs' game clocks part (0:09.03
against 0:08.98 at frame 2,600): the DLLs' own instructions count towards
`-deterministic`'s clock, so the cars are elsewhere by the later frames.
Screenshots in the session's scratch folder, not in the repo.

**On the GPU** (November 2026). The stand-in's GPU backend
(docs/common/graphics/glide.md, The GPU backend) draws the same pixels by the
same rules. `nfs-host -glidegpucompare out.csv` draws every call on the
GPU and in software and compares each frame at its buffer swap; the same
run as above (5 billion instructions, Release build), under this
container's software Vulkan (llvmpipe):

| | frames | mean difference (of 255) | pixels differing, mean (most) | off by more than 24, most |
|---|---|---|---|---|
| Retail DLL, menus (swaps 0-1,900) | 1,899 | 0.0018 | 233 (1,305) | 1 |
| Retail DLL, loading screen (1,950-2,250) | 300 | 0.0001 | 15 (118) | 3 |
| Retail DLL, race (2,400 on) | 1,510 | 0.0010 | 133 (228) | 3 |
| Voodoo3 patch DLL, all 3,829 | 3,829 | 0.0014 | 181 (1,305) | 3 |

The differences are single pixels: a texel or a dither step at a texel
boundary or depth, where the GPU evaluates each value's plane at the
pixel and the software rasterizer steps it along the row
(glide.md, Deviations).

Speed, the same runs without comparing (Release build; the times are
wall-clock with other work on the machine, and the GPU is llvmpipe on the
same CPU, so they say little about a real GPU): `-glide` 215 s in
software, `-glidegpu` 223 s; the patch DLL 256 s in software and 197 s on
the GPU (with less other work running then). On the
Play page (`renderer=glide`, Xvfb) the menus run at about 18 frames a
second either way and the game's attract-mode race at 9.8 on the GPU
against 9.3 in software (the patch DLL 9.3 on the GPU): the interpreter,
at about 135 million instructions a second, is the limit here, not the
drawing.

## Cost

Instructions a frame in the interpreter:

| | softtria | native drawing | and native flips and clears |
|---|---|---|---|
| Front end menu | 5.1 million | 0.5 million | |
| Race, Hometown, 8 cars | 16 million | 2.5 million, 0.6 million of them still in the DLL | 1.1 million, 0.03 million in the DLL |

A race now runs at 25 to 28 frames a second, and its clock keeps about
real time.
