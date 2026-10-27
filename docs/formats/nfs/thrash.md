# Need for Speed III: THRASH, the renderer interface

Status: **partly implemented**. Native drawing is in
`src/engines/nfs/host/thrash_native.cpp`; `reluva-tool nfs-host -native`
uses it. The renderer DLL still keeps its own books (textures, windows,
page flips). Native drawing takes over only its triangles, quads, lines
and points.

THRASH is the interface between `nfs3.exe` and its renderer DLLs:

- `softtria.dll` is the software renderer;
- `d3da.dll` runs on Direct3D;
- `3dfx.dll` runs on Glide.

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
| 0xe | stored as 1/value |
| 0xf | fog colour |
| 0x13 | eight dwords, copied |
| 0x15 | fog mode |
| 0x16-0x18 | fog values |
| 0x24 | alpha test, greater than 0 (the colour key) |
| 0x68 | blend: 0 source alpha / inverse source alpha, 1 source alpha / one, 2 zero / inverse source alpha, 3 destination colour / zero |
| 0x6a | z write |
| 0x190-0x195 | assorted; 0x193 returns capabilities, 0x194 and 0x195 set source and destination blend from tables |
| 0x2bd, 0x2bf, 0x2c2 | softtria's own; d3da ignores them and returns 0 |

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
  culling or clip rectangle changes. Clears are filled rectangles in the
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
