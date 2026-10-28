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
  by name), for 3dfx's Voodoo cards.

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
| 0x4 | z test and write; the game sends 0 with softtria |
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
to a folder, the Play page has the game drawn both ways, at 640x480 and
point-sampled. Every 60th frame it writes the GPU's frame and the
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
each frame's list: batches, vertices, textures, the flip's rectangle.

DirectDraw's frames (the movies) are shown over an empty frame. Frames
are drawn at `renderscale` times 640x480 (2 unless set): the same
picture, with sharper edges. Textures are point-sampled as in softtria,
or bilinear with `texturefilter=linear`. Both settings are in the game's
section of `reluva.ini`. Without a GPU, the software rasterizer draws
as before.

## Cost

Instructions a frame in the interpreter:

| | softtria | native drawing | and native flips and clears |
|---|---|---|---|
| Front end menu | 5.1 million | 0.5 million | |
| Race, Hometown, 8 cars | 16 million | 2.5 million, 0.6 million of them still in the DLL | 1.1 million, 0.03 million in the DLL |

A race now runs at 25 to 28 frames a second, and its clock keeps about
real time.
