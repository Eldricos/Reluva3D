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
  visuals and lights attached, and read back as arrays:
  `IDirect3DRMFrameArray`, `LightArray`, `VisualArray`), its transform and its parts (translation,
  scale, rotation, a matrix, each combined as asked), its position and
  orientation set and read relative to another frame, points taken to the
  world and back, its colour, material mode, sort and Z buffer modes, and
  a scene's background and fog.
- **`IDirect3DRMDevice`, `IDirect3DRMDevice2`:** on a Direct3D device: its
  size, quality, texture quality, shades, dither, buffer count, render
  mode, update callbacks (kept), and the Direct3D device back.
- **`IDirect3DRMViewport`:** its rectangle, camera, front and back planes,
  field, projection and scaling, its view's edges (`GetPlane`),
  `Clear`, to the camera's scene's background and the far depth,
  `Render`, drawing a frame and what's in it ([Drawing](#drawing)),
  `Transform` and `InverseTransform`, a point from the world to the
  screen and back, as a homogeneous `D3DRMVECTOR4D`: the pixel's x and
  y and the depth (0 to 1) times w, the clip space's w *(inferred: the
  documentation says only "a four-element homogeneous vector")*, and
  `Pick`, what's under a point ([Picking](#picking)), as an
  `IDirect3DRMPickedArray` and, for each pick, its frames as an
  `IDirect3DRMFrameArray`.
- **`IDirect3DRMLight`, `IDirect3DRMMaterial`:** their settings.
- **`IDirect3DRMTexture`, `IDirect3DRMTexture2`:** a surface's texture,
  its colours, shades and decal settings, and `Changed` (its surface's
  pixels or palette changed).
- **`IDirect3DRMMesh`:** groups of faces (`AddGroup`: a fixed number of
  vertices a face, or each face's count before it), their vertices
  (`D3DRMVERTEX`: position, normal, texture coordinates, colour), each
  group's colour, mapping, quality, material and texture, read back
  (`GetGroup`, `GetVertices`), scaled, moved, and its box.
- **Not yet:** in drawing, decals, frames' textures and Retained Mode's
  own lighting (colour ramps, shades); mesh builders,
  animations, move callbacks and velocities, loading files, and the
  other kinds of object.
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

### Drawing

`Render` draws the frame it's given, the frames in it and those made its
visuals, each with its world transform, through the Direct3D stand-in's
fixed-function pipeline ([directx.md](directx.md)), onto the viewport's
device; the device's own states are put back after. The camera looks
down its frame's z, y up. The projection maps the view's edges on the
front plane (`GetPlane`'s) to the viewport's, and the front plane to the
back to depths 0 to 1 *(inferred: the documentation gives the edges and
planes, not the matrix)*. The lights in the frames drawn are Direct3D's,
the ambient ones added together; a light shines down its frame's z, and
a spotlight's umbra and penumbra are taken as half its cones' angles
*(inferred)*.

Frames are drawn in order: a frame, the frames made its visuals, then
its children, each in the order added, each before what's in it
*(inferred)*. With sorted transparency (`D3DRMRENDERMODE_SORTEDTRANSPARENCY`)
the translucent groups are drawn last, farthest first (by a vertex's
depth), writing no depth *(inferred)*; with blended transparency alone,
each where it comes. The scene's fog (its frame's `SetSceneFog...`) is
Direct3D's, worked out at each vertex by its distance along the view:
linear, exponential, or exponential squared, from its start, end and
density.

A mesh whose box (around its vertices, kept until it changes) is wholly
outside the view is skipped, as most of a scene is off the screen. Each
mesh group's faces are drawn as triangle fans, clockwise faces
facing the camera, with the Z buffer if the device has one. A render
quality (`D3DRMRENDERQUALITY`) is a shade in bits 0 to 2 (flat 0,
Gouraud 1), lighting in 3 to 5 (on 8) and fill in 6 and 7 (solid
0x80); the group's quality and the device's together say whether it's
lit and how it's shaded. Lit (its normals made unit length, as a frame
may be scaled), its colour is its material's diffuse and ambient colour, with the material's specular power and colour and
emissive colour; unlit, its colour as it is. A colour less than opaque
is blended. A group's texture is its surface's pixels times its colour,
wrapping, filtered as the device's texture quality says.

### Picking

`Pick` takes a point of the device and finds, of each mesh in the
camera's scene, its nearest face under it, as the screen shows it
(either winding, in front of the front plane): the visual, its frames
from the scene down to the one holding it, the face and group, and the
point (x, y, and its depth, 0 to 1), nearest first. The documentation
gives the path "from the root down to the frame that contained the
object", and "depth-sorted"; the nearest first is *inferred*.

### Values returned

Methods returning a `D3DVALUE` (`GetField`, `GetPower`, a light's
`GetRange`...) return it on the x87 stack, as 32-bit Windows code takes
a float a function returns; methods returning a colour or a count return
it in `EAX`.

## Sources and checks

- **Where the facts come from:** Microsoft's DirectX 5 SDK documentation
  and headers: the interfaces, their methods and arguments, the IIDs, the
  defaults (a viewport's front 1, back 100 and field 0.5; a light's range
  256, umbra 0.4, penumbra 0.5), the combine types and render qualities,
  a viewport's plane ("the dimensions of the viewport on the front
  clipping plane") and its camera's axes, and what a pick holds
  (`D3DRMPICKDESC`).
- **How it was checked:** `common.unit.windows`'s `retained mode` case,
  on a DirectX 5 Direct3D device: frames placed and turned relative to
  each other and the world, points taken to the world and back, names,
  a device, a viewport's field and view edges and its clear to the
  scene's background, a light and a material, their floats on the x87
  stack, a texture on a surface, a mesh's group, vertices, box and face
  data read back, and a clone apart from its original; and a green square
  in front of a camera, drawn in the viewport's middle and not its
  corner, and picked there (its mesh, group and frames) but not at the
  corner; its middle taken to the viewport's middle pixel and back; lit
  by two white lights, textured mid-grey, drawn grey; and drawn across
  the view's edge, but not wholly beyond it; fogged; and two
  half-transparent squares drawn in the order added, or sorted, the
  far one first. LEGO Island draws its scenes with it, and picks in them
  ([running.md](../../engines/legoisland/systems/running.md)).
- **Confidence:** published unless marked.

## Used by

- **`legoisland`**: LEGO Island's 3D, through its device made on
  DirectX 5's Direct3D
  ([running.md](../../engines/legoisland/systems/running.md)).
