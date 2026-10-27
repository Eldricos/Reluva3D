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

## Cost

Instructions a frame in the interpreter:

| | softtria | native drawing |
|---|---|---|
| Front end menu | 5.1 million | 0.5 million |
| Race, Hometown, 8 cars | 16 million | 2.5 million, 0.6 million of them still in the DLL |

What the DLL still runs is mostly `pageflip` (0.3 million a frame:
copying 640x480 pixels) and texture uploads. Both can be taken over
next.
