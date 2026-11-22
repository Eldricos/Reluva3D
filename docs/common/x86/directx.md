# DirectDraw, Direct3D 6 and 7, and DirectInput

## What it is

`src/emulation/x86/com.{h,cpp}` makes COM objects programs can call. Each
interface is a vtable of native functions in the program's memory,
dispatching to a C++ object. `directx/` has the objects, written from the
DirectX 7 documentation, and Direct3D 6's (`directx/d3d6.cpp`) from the
DirectX 6 documentation. Interfaces, method orders and IIDs are the SDK
headers' (`directx/interfaces.cpp`).

## What Reluva supports

- **DirectDraw:**
  - one adapter with 3D;
  - the modes `DirectX::modes` lists;
  - surfaces in the program's memory, as programs lock them and write to
    them;
  - blits (stretching, colour keys, colour and depth fills) and flipping
    chains; a blit keyed by its source's colour key, from a source with
    none, fails with `DDERR_NOCOLORKEY`, as DirectDraw's documentation
    gives it (Starfleet Command II then draws its pointer itself);
  - palettes and clippers;
  - the display's gamma ramp (`IDirectDrawGammaControl`, from any
    surface).

  What reaches the primary surface becomes `DirectX::frame()`, handed to
  the engine, through the gamma ramp. In a window, that's the window's
  client area. With `DirectX::presentLate` a flip hands over the frame
  before, once its draws are done, and the program goes on while this
  one's are drawn.
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
- **Direct3D 6:** `IDirect3D3` (from `IDirectDraw4`, as Direct3D 7 is
  from `IDirectDraw7`) and its device, `IDirect3DDevice3`, which is
  Direct3D 7's device under a second interface: the same states,
  transforms, texture stages and drawing. What Direct3D 6 does its own
  way:
  - the devices' descriptions (`D3DDEVICEDESC`, 252 bytes), a hardware
    one and a software one for each device: the software device's
    hardware description empty, so a program tells them apart;
    `FindDevice`;
  - viewports (`IDirect3DViewport3`): a rectangle and a clip volume
    (`D3DVIEWPORT2`, or `D3DVIEWPORT`'s ±`dvMaxX` by ±`dvMaxY`), mapped
    after the projection onto Direct3D 7's; a background material, whose
    diffuse colour `Clear` fills with; `Clear2`; their lights;
  - materials (`IDirect3DMaterial3`) by handle, which the `MATERIAL`
    light state names; lights (`IDirect3DLight`, `D3DLIGHT` and
    `D3DLIGHT2`), their one colour diffuse and specular, on while
    `D3DLIGHT_ACTIVE`; the other light states as Direct3D 7's render
    states (`AMBIENT`, the fog's, `COLORVERTEX`);
  - lighting by draw: vertices with normals are lit unless the draw's
    flags say `D3DDP_DONOTLIGHT`;
  - `IDirect3DTexture2` on texture surfaces, with handles, which the
    `TEXTUREHANDLE` render state also takes; `Load`;
  - `IDirect3DVertexBuffer`, Direct3D 7's under its older interface;
    `DrawIndexedPrimitiveVB` with Direct3D 6's arguments.

  Not stood in for: `Begin`, `Vertex` and `End`;
  `IDirect3DViewport3::TransformVertices` and `LightElements`; the other
  legacy texture render states (`TEXTUREMAPBLEND` and the like).
  `ComputeSphereVisibility` says every sphere is wholly inside. Lights'
  attenuation is taken as Direct3D 7's *(guessed: Direct3D 6's is
  described the same way)*.
- **`directx/dinput.cpp`: DINPUT.dll**, DirectInput up to DirectX 7.
  - The system mouse (3 axes, 4 buttons) and keyboard (256 keys by scan
    code), read through the program's data format (the `c_dfDIMouse` and
    `c_dfDIKeyboard` kinds, or its own).
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

- **Where the facts come from:** the DirectX 7 documentation, the
  DirectX 6 documentation for Direct3D 6, and the SDK headers for
  interfaces, method orders and IIDs.

- `testDirectX` in `tests/common/windows.cpp` (`common.unit.windows`) checks these, called through
  the vtables: a clear, a Gouraud triangle, the depth test, a textured
  quad with point sampling, and a flip.

- `testDirect3D6` checks Direct3D 6 the same way: the devices'
  descriptions, a viewport cleared to its background material, unlit
  draws through two clip volumes, a lit draw coloured by the material
  the `MATERIAL` light state names, and a texture's handle.

- `testInput` checks DirectInput's mouse (its state, its movement since
  the last read, its buffer) and keyboard.
- **Confidence:** published unless marked.

## Used by

- **`sfc`**: Starfleet Command II draws through DirectDraw 4 and
  Direct3D 6 ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md)).

- **`netimmerse`**: LEGO Alpha Team draws through DirectDraw and Direct3D 7
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
  It doesn't read DirectInput
  ([input.md](../../engines/netimmerse/games/legoalphateam/systems/input.md)).
