# Boggle's DirectX text meshes

Sources: the four `Scripts/*.x` files in the owner's install, identified by
whole-file SHA-256 in the [manifest](../../../plans/boggle-1997/install-manifest.tsv).
The header at byte zero is `xof 0302txt 0064`. Geometry, index/normal/UV counts,
separators and inline material blocks were inspected directly. No game code
or third-party implementation was copied. Tests construct a separate generic
triangle and its own material.

The current reader accepts this observed subset:

- The exact text header above, optional `Header { 1; 0; 1; }`, and one Mesh
  (optionally named).
- Vertex triples and polygon index lists; faces retain their winding and
  corner order. There is no implicit triangulation, coordinate conversion
  or inferred smoothing.
- `MeshMaterialList`, with one material index per face and inline Material
  blocks: RGBA diffuse, power, RGB specular and RGB emissive. `Shadow.x`
  omits emissive; the reader supplies black for that observed omission.
- `MeshNormals`, retaining separate per-corner normal indexes, and
  `MeshTextureCoords`, with one pair per vertex. These blocks may be absent;
  if present, duplicate blocks and mismatched topology/counts fail.

Finite doubles are parsed with the classic locale and numeric tokens bounded
to 128 characters. Vertex/normal/UV and
face-list counts are capped at 65,536 each; material counts at 1,024;
individual polygons at 256 corners; total position/normal face corners at
1,048,576; input at 16 MiB. Counts are checked against remaining input before
allocation. Index bounds, list separators, tuple lengths and closing braces
are checked. Output is cleared on failure.

Binary/compressed `.x`, template declarations, Frames/Animations, material
references, texture filenames and unknown blocks are explicitly unsupported.
No external path in a mesh is followed. This is not a general DirectX loader.

All four supplied meshes pass: 712 vertices and 222 polygon faces in total.
Those aggregate counts do not imply that the scene, materials or animation
behavior has been reproduced. A rendering layer will need explicit handling
of geometry conventions, palette mode and material behavior.
