# Direct3D Retained Mode

## What it is

`src/emulation/x86/directx/d3drm.{h,cpp}` stands in for `d3drm.dll`,
Direct3D Retained Mode up to DirectX 5, for programs run in the x86
interpreter. `Direct3DRMCreate` makes the Retained Mode object, which
makes the others: frames, devices, viewports, lights, materials. It's
written from Microsoft's DirectX 5 documentation: its interfaces, their
methods' order, the IIDs, structures and constants are the SDK's
(`d3drm.h`, `d3drmobj.h`, `d3drmdef.h`). It's built on the DirectDraw and
Direct3D stand-ins ([directx.md](directx.md)), whose surfaces and devices
it draws to.

## What Reluva supports

So far what LEGO Island calls as it loads its world and plays its intro:

- **`IDirect3DRM`, `IDirect3DRM2`:** making frames, devices from a
  Direct3D device (`CreateDeviceFromD3D`), viewports, lights (`CreateLight`,
  `CreateLightRGB`), materials, meshes (`CreateMesh`) and textures on
  DirectDraw surfaces (`CreateTextureFromSurface`); the default texture colours and shades;
  the search path; `Tick`, which moves nothing yet.
- **Every object (`IDirect3DRMObject`):** its name, the program's data,
  destroy callbacks (kept, not yet called), its class's name, and `Clone`
  for meshes, lights, materials and textures (a mesh's copy has its own
  groups, sharing their materials and textures).
- **`IDirect3DRMFrame`, `IDirect3DRMFrame2`:** the hierarchy (children,
  visuals and lights attached), its transform and its parts (translation,
  scale, rotation, a matrix, each combined as asked), its position and
  orientation set and read relative to another frame, points taken to the
  world and back, its colour, material mode, sort and Z buffer modes, and
  a scene's background and fog.
- **`IDirect3DRMDevice`, `IDirect3DRMDevice2`:** on a Direct3D device: its
  size, quality, texture quality, shades, dither, buffer count, render
  mode, update callbacks (kept), and the Direct3D device back.
- **`IDirect3DRMViewport`:** its rectangle, camera, front and back planes,
  field, projection and scaling, its view's edges (`GetPlane`), and
  `Clear`, to the camera's scene's background and the far depth.
- **`IDirect3DRMLight`, `IDirect3DRMMaterial`:** their settings.
- **`IDirect3DRMTexture`, `IDirect3DRMTexture2`:** a surface's texture,
  its colours, shades and decal settings, and `Changed` (its surface's
  pixels or palette changed).
- **`IDirect3DRMMesh`:** groups of faces (`AddGroup`: a fixed number of
  vertices a face, or each face's count before it), their vertices
  (`D3DRMVERTEX`: position, normal, texture coordinates, colour), each
  group's colour, mapping, quality, material and texture, read back
  (`GetGroup`, `GetVertices`), scaled, moved, and its box.
- **Not yet:** drawing (`Render` draws nothing yet), mesh builders,
  animations, move callbacks and
  velocities, picking, loading files, and the other kinds of object.
  Calling a method nothing stands in for ends the run naming it.

## How it works

### Frames and their transforms

A frame's transform is a `D3DRMMATRIX4D` as DirectX writes it: row
vectors, a point times the matrix, the last row the translation, in a
left-handed space. A frame's world transform is its own times its
parent's, and so on up to its scene, the frame with no parent.

The `Add` methods combine a new transform with the frame's: replacing it
(`D3DRMCOMBINE_REPLACE`), before it (`BEFORE`: the new one applied
first) or after it (`AFTER`). A rotation is about an axis through the
frame's origin, by an angle in radians.

The methods taking a reference frame (`SetPosition`, `GetPosition`,
`SetOrientation`, `GetOrientation`) work in that frame's terms; with no
reference, in the frame's parent's *(inferred)*. An orientation is a
direction (the frame's z axis) and an up (its y axis, made at right
angles to the direction); x is up cross direction.

### Values returned

Methods returning a `D3DVALUE` (`GetField`, `GetPower`, a light's
`GetRange`...) return it on the x87 stack, as 32-bit Windows code takes
a float a function returns; methods returning a colour or a count return
it in `EAX`.

## Sources and checks

- **Where the facts come from:** Microsoft's DirectX 5 SDK documentation
  and headers: the interfaces, their methods and arguments, the IIDs, the
  defaults (a viewport's front 1, back 100 and field 0.5; a light's range
  256, umbra 0.4, penumbra 0.5), and the combine types.
- **How it was checked:** `common.unit.windows`'s `retained mode` case,
  on a DirectX 5 Direct3D device: frames placed and turned relative to
  each other and the world, points taken to the world and back, names,
  a device, a viewport's field and view edges and its clear to the
  scene's background, a light and a material, their floats on the x87
  stack, a texture on a surface, a mesh's group, vertices, box and face
  data read back, and a clone apart from its original. LEGO Island builds its scene with it and goes on to its main loop
  ([running.md](../../engines/legoisland/systems/running.md)).
- **Confidence:** published unless marked.

## Used by

- **`legoisland`**: LEGO Island's 3D, through its device made on
  DirectX 5's Direct3D
  ([running.md](../../engines/legoisland/systems/running.md)).
