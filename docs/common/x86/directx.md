# DirectDraw, Direct3D and DirectInput

## What it is

`src/emulation/x86/com.{h,cpp}` makes COM objects programs can call. Each
interface is a vtable of native functions in the program's memory,
dispatching to a C++ object. `directx/` has the objects, written from the
DirectX 7 documentation, and DirectX 5's for its Direct3D (`d3d5.cpp`). Interfaces, method orders and IIDs are the SDK
headers' (`directx/interfaces.cpp`).

## What Reluva supports

- **DirectDraw:**
  - one adapter with 3D;
  - the modes `DirectX::modes` lists;
  - surfaces in the program's memory, as programs lock them and write to
    them;
  - blits (stretching, colour keys, colour and depth fills) and flipping
    chains;
  - palettes and clippers;
  - a surface's device context (`GetDC`, `ReleaseDC`), drawing GDI's
    text and bitmaps into it in its own format
    ([windows.md](windows.md));
  - the display's gamma ramp (`IDirectDrawGammaControl`, from any
    surface).

  What reaches the primary surface becomes `DirectX::frame()`, handed to
  the engine, through the gamma ramp. In a window, that's the window's
  client area. With `DirectX::presentLate` a flip hands over the frame
  before, once its draws are done, and the program goes on while this
  one's are drawn. So does a blt to the primary of a plain copy (the
  same format and size, no colour key) from a surface a device draws to:
  the copy is made by the draw threads, each row by the thread that
  draws it, after that frame's draws and before the next's, and the blt
  shows the frame before. LEGO Island's island frames are 40 to 54%
  quicker for it on four processors, the same pixels a frame later.
- **Direct3D 7:** RGB emulation and HAL devices, vertex buffers and state
  blocks. The drawing is a software rasterizer (`directx/raster.cpp`),
  which Retained Mode's stand-in draws through too, its vertices the
  host's ([d3drm.md](d3drm.md#drawing)):
  - the fixed-function pipeline: transforms, lighting (a lit colour
    clamped to 0 to 1 at the vertex), materials and their vertex-colour
    sources, and texture coordinate generation and
    transforms;
  - clipping to the view volume, and culling by winding; a flat-shaded
    triangle keeps its first vertex's colour however clipping cuts it;
  - pixel centres at whole coordinates, with a top-left fill rule;
  - perspective-correct interpolation;
  - up to 8 texture stages with all the D3D7 operations from DISABLE to
    BLENDCURRENTALPHA, mip levels and bilinear filtering;
  - colour keys, the alpha test, vertex and table fog, all the blend
    factors, and the depth test into any Z format.
- **Direct3D 5** (`directx/d3d5.cpp`): `IDirect3D2`, which the DirectDraw
  object gives for its IID, offers the same two devices, described in
  DirectX 5's `D3DDEVICEDESC` (204 bytes): `EnumDevices`, with hardware
  and software descriptions, each with its flags only on its own side;
  `FindDevice`, by GUID and by hardware; and `CreateDevice`. The device
  is the Direct3D 7 one, as `IDirect3DDevice2`: its methods of the same
  name and arguments as Direct3D 7's are those; `GetCaps`,
  `EnumTextureFormats` (each format in a `DDSURFACEDESC`) and
  `GetDirect3D` are DirectX 5's own. Not yet: lights, materials and
  viewports (`IDirect3DLight`, `IDirect3DMaterial2`, `IDirect3DViewport2`),
  execute buffers, and drawing through `IDirect3DDevice2`.
- **`directx/dinput.cpp`: DINPUT.dll**, DirectInput up to DirectX 7.
  - The system mouse (3 axes, 4 buttons) and keyboard (256 keys by scan
    code, an extended key's with the top bit set: the up arrow, 0x48
    extended, is `DIK_UP`, 0xc8), read through the program's data format
    (the `c_dfDIMouse` and `c_dfDIKeyboard` kinds, or its own).
  - Read either as an immediate state (relative or absolute axes) or as a
    buffer of changes (`DIPROP_BUFFERSIZE`, `GetDeviceData`).

## How it works

### Drawing quickly

How it's kept quick, each change giving the same pixels:
- A draw's triangles are drawn by rows on as many threads as there are
  processors (`RowThreads`). Each thread takes groups of 8 rows, dealt
  round, and draws every triangle there in order. Small draws stay on
  one thread. `DirectX::drawThreads` can choose one thread, or threads
  always, which the test uses to check both give the same image.
- Threads with nothing to draw sleep, and are woken only once the draws
  given since come to 32K pixels (by their bounding boxes), or something
  waits for them; those awake take each draw as it comes. Waking them for
  every small draw took a fifth of LEGO Island's thread; held, its island
  frames are 12 to 15% quicker on four processors.
- A triangle draw is given to the draw threads as its lit vertices: the
  first thread to come to it clips, projects and culls its triangles,
  for all of them, so the program's thread doesn't (a fifth of LEGO
  Island's). On four processors the island is no quicker for it, as its
  frames are then drawn as fast as the draw threads can; with more, it
  leaves the program's thread free. (Each thread setting up a share of
  the triangles, the others waiting for it, was far slower: every
  thread held to the slowest at each draw.)
- Each row is filled from its first inside pixel to its last; the edge
  tests are only made at the ends.
- The depth test comes before a pixel's attributes are worked out.
- Samplers are set up per triangle: the mip level, and power-of-two
  wrapping by mask. Bilinear sampling indexes directly when all four
  texels are inside.
- Stages that only select or modulate plain arguments (all of LEGO
  Alpha Team's) are combined from sources picked once per draw. The general
  path handles the rest.
- Targets, depth buffers and textures are read and converted through
  tables (`ArgbConverter`), also for presenting a frame.
- With SSE2 (every x86-64 build), rows go four pixels at a time
  (`Draw::span4`) where a draw's pixels take the quick stages into a
  16- or 32-bit RGB target, with no colour key, the usual blend or
  none, and linear, vertex or no fog: all of LEGO Alpha Team's. The depth
  test on a 16-bit buffer, bilinear or point sampling of wrapping
  power-of-two textures (`sample4`, 16-bit channels), the stages, fog,
  blending and the conversion to the target are each the scalar code's
  operations in the same order, so the same bits (no fused
  multiply-adds either way). Anything else falls back to one pixel at
  a time within the group.

### Software first

Software first, rather than `gfx::Renderer3D`: programs such as LEGO
Alpha Team lock and blit surfaces as well as drawing into them, and doing
both in one place keeps them consistent. It also lets the frames be
checked without a GPU.

### Drawing on the GPU

With `DirectX::DrawThreads::kGpu`, triangles are drawn on the GPU by the
software rasterizer's own rules (`dx::GpuRaster`, `directx/gpu_raster.cpp`,
`directx/shaders/rasterize.comp`), as the Glide stand-in's GPU backend
does ([glide.md](../graphics/glide.md#the-gpu-backend)), through
`gfx::Compute` on a GPU device of its own. Where there's none, the first
draw finds out, says so in the process's output (as it names the GPU when
there is one), and `drawThreads` becomes `kAuto`. LEGO Island uses it unless
its *3D drawn by* setting is *The processor*; `reluva-tool legoisland run -gpu`
does too.

- **Set-up on the CPU, as the software one does it** (`Draw::rasterize`'s
  own steps, in doubles): each triangle's pixels on each row, exactly, so
  which pixels are drawn is the same by construction; the planes of z,
  1/w and the attributes divided by w; each stage's mip level and its
  filter.
- **Batches and tiles.** A draw's state goes into a record (a new one
  only when it changes), each triangle into the batch with its rows'
  spans. At a flush the batch is binned into 16x16 tiles in drawing
  order; the kernel runs a workgroup a tile, a thread a pixel, through
  the tile's triangles in order, so depth and blending see what the
  earlier ones wrote.
- **The pixel pipeline in the kernel**, from `Draw::shade`, `sample` and
  `finish`: the depth test, every texture stage operation and argument
  modifier, point and bilinear sampling with the software one's 8-bit
  weights and integer mixing, the address modes and border colour, the
  colour key (the nearest texel's), specular, the alpha test, vertex and
  table fog, every blend factor, and the write to a 565 or X888 target.
  Other targets, and depth buffers other than 2 or 4 bytes a pixel, are
  drawn on the CPU, after what's waiting on the GPU.
- **Clears, fills and copies on the GPU too.** A device's clear, a blt's
  colour or depth fill, and a blt copy (unstretched, in the same format,
  keyed by a source colour key or not) onto a surface the GPU keeps go into
  the batch in their turn, the copy with its source's pixels as they are
  then. Other targets, depth buffers other than 2 or 4 bytes a pixel,
  lines and points are drawn on the CPU, after what's waiting on the GPU.
- **Surfaces kept on the GPU.** Each target and depth buffer drawn into
  has a copy there (a word a pixel, a buffer each), and beside it its
  words as the two last agreed. Before a batch, the rows whose memory
  differs from those are sent; after it, nothing comes back until
  something touches the surface's pixels: a lock, a blt from or onto it, a
  device context, a present of the primary. Then the rows the GPU drew
  since are brought back into its memory (`GpuRaster::sync`), for that
  surface alone; a flip, a mode change or restoring surfaces bring back
  every one. A surface gone, or whose memory has moved (a flip), is
  forgotten, what the GPU drew brought back first while the memory is
  still its. On LEGO Island's island this sends next to nothing and
  brings back only the back buffer's drawn rows, once a frame, for the
  present: the depth buffer never comes back.
- **Textures** go into one buffer, as ARGB words with a bit a texel for
  the colour key, kept while their surfaces' versions don't change, and
  started again once it's 256 MB.

Deviations from the software rasterizer:
- **The planes per pixel.** The kernel works each value out at each pixel
  in 32-bit floats; the software one works it out at the row's start and
  adds its step pixel by pixel. A value right at a rounding boundary can
  then come out a step apart: on LEGO Island's island, about 1 pixel in
  130 of a frame is a 565 step off, scattered over textured faces.
- **Depth** is worked out in floats, not doubles: exact for 16-bit depth
  buffers, a little off for deeper ones.
- **The depth buffer's memory** isn't what the GPU drew until something
  reads it, so the process's memory (and a fixed-clock run's state hash)
  differs from the software rasterizer's, with the same frames.

## Sources and checks

- **Where the facts come from:** the DirectX 7 documentation, and the SDK
  headers for interfaces, method orders and IIDs; DirectX 5's
  documentation and headers for its Direct3D.

- `testDirectX` in `tests/common/windows.cpp` (`common.unit.windows`) checks these, called through
  the vtables: a clear, a Gouraud triangle, the depth test, a textured
  quad with point sampling, and a flip.

- The GPU's drawing is checked against the software one's in
  `testDirectX`: the same 35 random triangles, textured, blended and
  depth tested, with a clear, a colour fill and a keyed copy among them,
  each pixel at most a 565 step apart (without a GPU they're drawn as with
  one thread). On LEGO Island, run on the fixed clock with
  `-gpu`, the Information Center's frames (2001, 4001) are the same to the
  pixel, and the island's (6001) a step apart in 2,267 of 307,200.

- `testDirect3D5` checks DirectX 5's Direct3D as LEGO Island reaches it:
  `IDirect3D2` from a DirectDraw object, its devices listed and found,
  and a device made on a surface, with its descriptions, texture formats
  and render states.

- `testInput` checks DirectInput's mouse (its state, its movement since
  the last read, its buffer) and keyboard.
- **Confidence:** published unless marked.

## Used by

- **`legoisland`**: LEGO Island makes its device through DirectX 5's
  Direct3D, for Retained Mode
  ([running.md](../../engines/legoisland/systems/running.md)).

- **`netimmerse`**: LEGO Alpha Team draws through DirectDraw and Direct3D 7
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
  It doesn't read DirectInput
  ([input.md](../../engines/netimmerse/games/legoalphateam/systems/input.md)).
