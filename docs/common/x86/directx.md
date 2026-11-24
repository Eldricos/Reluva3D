# DirectDraw, Direct3D 7 and DirectInput

## What it is

`src/emulation/x86/com.{h,cpp}` makes COM objects programs can call. Each
interface is a vtable of native functions in the program's memory,
dispatching to a C++ object. `directx/` has the objects, written from the
DirectX 7 documentation. Interfaces, method orders and IIDs are the SDK
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
  - a surface's device context (`GetDC`), for GDI's text: colours become
    the surface's pixels, a palettized surface's as its palette's nearest
    entry;
  - the display's gamma ramp (`IDirectDrawGammaControl`, from any
    surface).

  What reaches the primary surface becomes `DirectX::frame()`, handed to
  the engine, through the gamma ramp. In a window, that's the window's
  client area. A program drawing its window through GDI instead
  (`Win32Process::windowDrawn`) has each blit's picture handed over the
  same way. A frame says where it is on the screen (`Frame::x`, `y`: the
  client area's corner in a window, 0, 0 full screen), so the engine
  can put the pointer where the player points on the picture. With `DirectX::presentLate` a flip hands over the frame
  before, once its draws are done, and the program goes on while this
  one's are drawn.
- **Direct3D 1 and 2** (DirectX 3 and 5), on the same DirectDraw object:
  `IDirect3D` and `IDirect3D2`, listing the same RGB emulation and HAL
  devices with DirectX 5's `D3DDEVICEDESC` (204 bytes), a description for
  hardware and one for software each. A software device's hardware
  description has only its size: a colour model of 0 is how programs tell
  the two kinds apart.
- **Direct3D 1's device** (`IDirect3DDevice`), as DirectX 3 and 5
  programs make it: asked of a 16- or 32-bit RGB surface by the device's
  IID (`QueryInterface`, COM's aggregation), one a surface. It draws
  through a Direct3D 7 device on the surface, so it draws as that does.
  - **Execute buffers** (`IDirect3DExecuteBuffer`) in the program's
    memory, their D3DEXECUTEDATA's vertices and instructions run by
    `Execute`: `PROCESSVERTICES` into a pool of 65,536 vertices
    (copied as `D3DTLVERTEX`; to be transformed as `D3DLVERTEX`; or
    transformed and lit as `D3DVERTEX`), `TRIANGLE`, `LINE`, `POINT`,
    `STATERENDER`, `STATETRANSFORM` and `STATELIGHT` (the material and
    ambient light), `MATRIXLOAD`, `MATRIXMULTIPLY`, `TEXTURELOAD`,
    `BRANCHFORWARD`, `SETSTATUS` and `EXIT`; `SPAN` isn't drawn.
  - **Render states** are Direct3D 7's but for those it replaced with
    texture stages: the texture handle, address, filters and border
    colour go to the first stage, and `TEXTUREMAPBLEND` to its
    operations as the DirectX 6 documentation maps the legacy blends
    (DECAL and COPY the texture, MODULATE its colour times the vertex's
    and its own alpha if it has one, DECALALPHA, MODULATEALPHA, ADD).
    With no texture the vertex's colour is drawn. Culling starts off, as
    Direct3D 1's did.
  - **Matrices** by handle (`CreateMatrix`, `SetMatrix`), **viewports**
    (`IDirect3DViewport` and `IDirect3DViewport2`: the area drawn to, and
    `Clear` to the background material's colour), **materials** by handle,
    and **lights** (kept, not lit with).
  - **Textures:** a texture surface's `IDirect3DTexture` and
    `IDirect3DTexture2` are its own: `GetHandle`, and `Load` copying
    another's pixels and palette.
- **Direct3D 7:** RGB emulation and HAL devices, vertex buffers and state
  blocks.
- **Direct3D 7:** RGB emulation and HAL devices, vertex buffers and state
  blocks. The drawing is a software rasterizer (`directx/raster.cpp`):
  - the fixed-function pipeline: transforms, lighting, materials and their
    vertex-colour sources, and texture coordinate generation and
    transforms;
  - clipping to the view volume, and culling by winding;
  - pixel centres at whole coordinates, with a top-left fill rule;
  - perspective-correct interpolation;
  - up to 8 texture stages with all the D3D7 operations from DISABLE to
    BLENDCURRENTALPHA, mip levels and bilinear filtering;
  - colour keys, the alpha test, vertex and table fog, all the blend
    factors, and the depth test into any Z format.
- **`directx/dinput.cpp`: DINPUT.dll**, DirectInput up to DirectX 7.
  - The system mouse (3 axes, 4 buttons) and keyboard (256 keys by scan
    code), read through the program's data format (the `c_dfDIMouse` and
    `c_dfDIKeyboard` kinds, or its own).
  - Read either as an immediate state (relative or absolute axes) or as a
    buffer of changes (`DIPROP_BUFFERSIZE`, `GetDeviceData`).
  - A key is its scan code, an extended key's with 0x80 (the up arrow,
    0x48 extended, is `DIK_UP`, 0xc8), as DirectInput numbers them.
  - `DirectInput::mouseHeld` tells the engine when the program has the
    mouse acquired exclusively: it reads the mouse's movement then, and
    Windows' pointer is hidden, so the engine holds the mouse in its
    window for it.

## How it works

### Drawing quickly

How it's kept quick, each change giving the same pixels:
- A draw's triangles are drawn by rows on as many threads as there are
  processors (`RowThreads`). Each thread takes groups of 8 rows, dealt
  round, and draws every triangle there in order. Small draws stay on
  one thread. `DirectX::drawThreads` can choose one thread, or threads
  always, which the test uses to check both give the same image.
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
  headers for interfaces, method orders and IIDs; for Direct3D 1 and 2,
  the DirectX 5 documentation and SDK headers (`directx/d3d5.cpp`).

- `testDirectX` in `tests/common/windows.cpp` (`common.unit.windows`) checks these, called through
  the vtables: a clear, a Gouraud triangle, the depth test, a textured
  quad with point sampling, and a flip; GDI's text through the back
  buffer's device context; and Direct3D 1's device list, its
  descriptions' sizes and colour models. `testDirect3D1` makes a version 1
  device of a back buffer, clears its viewport to a material's colour,
  draws a square of two triangles from an execute buffer of
  pre-transformed vertices, and loads a texture by its handle.

- `testInput` checks DirectInput's mouse (its state, its movement since
  the last read, its buffer) and keyboard.
- **Confidence:** published unless marked.

## Used by

- **`netimmerse`**: LEGO Alpha Team draws through DirectDraw and Direct3D 7
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
  It doesn't read DirectInput
  ([input.md](../../engines/netimmerse/games/legoalphateam/systems/input.md)).
- **`sith`**: Jedi Knight's DirectDraw, Direct3D 1's devices (when its
  Setup chooses 3D acceleration) and DirectInput ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
