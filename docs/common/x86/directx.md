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

## Sources and checks

- **Where the facts come from:** the DirectX 7 documentation, and the SDK
  headers for interfaces, method orders and IIDs; DirectX 5's
  documentation and headers for its Direct3D.

- `testDirectX` in `tests/common/windows.cpp` (`common.unit.windows`) checks these, called through
  the vtables: a clear, a Gouraud triangle, the depth test, a textured
  quad with point sampling, and a flip.

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
