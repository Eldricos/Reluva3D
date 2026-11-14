# Classic complete cube renderer

Installed-artwork mode now renders complete cube bodies and upward lettering
from the validated `.x` resources. Original and Master use the same software
pipeline, verified camera poses and measured 1.1-unit grid spacing. Cube centers,
mouse picking and computer pointer destinations share the 3D projection.
Round rules, board generation and accepted-word/scoring logic are unchanged.

## Resources and compatibility

[Cube style](cube-style.md) in launcher game options selects Automatic, Rounded
or Angular. Automatic retains the preference for `Scripts/Rounded.x`, otherwise
`Scripts/Die.x`, with the flat fallback when neither exists. Explicit selections
load only that resource and report a missing selected mesh. This is a native
control; the original rounded-style option label/default still await VM comparison.
Both styles are validated, and external previews exercise the supplied scenes.

The existing 55-file import already includes these two optional scripts.
Re-import if your library predates [cube geometry preparation](cube-geometry.md).
Missing scripts retain the previous flat-face view. Malformed selected scripts
report an error. Disabling Classic installed artwork retains the prototype and
skips mesh loading. No additional imports or dependencies are introduced.

## Geometry, lettering and picking

The renderer keeps the complete body polygons, UVs, materials and per-corner
normals. Generated boards retain each die's six supplied faces and selected
side. [Die orientation](die-orientation.md) records the measured input axes and
settled poses for both mesh styles. The selected side faces upward; the other
five letter groups receive their actual supplied letters, including Qu.
Manually injected boards have no physical die metadata and retain blank sides.

Auto-orient keeps the upward glyph upright. Disabling it rotates the complete
cube, normals and lettering by a stable native quarter turn. Drawing, picking,
review markers and computer destinations share that physical pose. Replay
retains it; New Board refreshes it. The native shuffle and top-letter draws are
unchanged. Side glyph UVs use the measured [template mapping](cube-textures.md),
including the rounded-face U mirrors. Exact original rasterization remains unverified.

Planar polygons use independently written ear clipping, including concave
polygons and collinear corners. Some supplied shoulder quads are folded in 3D;
they use the original vertex-order diagonal rather than being flattened into a
crossed 2D outline. Matching the original tessellation of every folded polygon
remains unverified. Crossed planar boundaries and unsupported topology fail.
The two resources produce 350 triangles in total (306 rounded, 44 angular).

Each triangle is projected through the existing camera. A reciprocal-depth
buffer resolves body/letter and inter-cube occlusion. UV and vertex-color
interpolation use perspective weights. Integer raster bounds, front-plane
rejection and division before attribute multiplication bound all intermediates
to portable 64-bit arithmetic, including Visual Studio's C++ implementation.
Triangles crossing the front plane are rejected; the observed Classic cameras
remain well away from it. No GPU or legacy DirectX runtime is required.

Picking evaluates the same projected triangles and selects the nearest visible
cube. Computer destinations use the upward-face center. Selection adds a native
cyan tint; completed-word review retains numbered path markers. These display
choices do not change adjacency or word eligibility.

## Lighting evidence and policy

Read-only scene initialization at `0x44e19c..0x44e1ad` supplies grey ambient RGB
(0.7,0.7,0.7). Directional creation at `0x44e253..0x44e264` supplies white RGB
(1,1,1). The following calls at `0x44e29e..0x44e2bf` orient its frame by a
135-degree Y rotation and 45-degree X rotation. The wrapper signatures/axes are
established at `0x45c480` and `0x45c4b0`. An external retained-mode frame-math
probe records the resulting forward vector approximately (0.5,-0.707107,-0.5).
Original files and that probe stay outside every Git worktree.

The native renderer uses these measured inputs in a Lambert diffuse model,
adds material emissive color, clamps to 8-bit RGB at vertices and interpolates
those colors across the triangles. The resource's normals supply smooth body
shading. This is native Gouraud shading, not a claim of matching Direct3D's full
lighting/rasterization pipeline. Specular response, cast shadows, palette
quantization and exact original light conventions remain comparison work.
No original renderer or third-party implementation code is used.

## Integration and validation

The same body renderer draws live play, round-end review and settled dice during
lid transitions. Observed per-frame source rectangles still mask the lid
composition, and transition frame selection/timer guards are unchanged.
Ready screens continue to hide the board.

Invented-cube tests cover planar/concave/folded topology, collinear corners,
malformed input, body occlusion, materials, Qu, rotation, clipping and bounded
near-plane arithmetic. Both mesh styles and both variants check every cube
center against picking, compare the word panel with the legacy view, verify lid
masks, and enter a word by mouse through the game loop with the same score.
Existing legacy rendering/gameplay records remain unchanged.

The supplied artwork checker renders 26 states and now reports
`ff08f12fd6fbda7487ff2e8702b60178`; install, CD and fresh-import checks agree.
`cube-check` also validates tessellation of both resources and reports the 350
triangles. Its geometry digest stays `e985c393b66b88890345759e0ab219fc` because
resource data and normalization did not change. Previews of both styles and
variants were inspected outside Git; no retail images or vertices are fixtures.

The full normal run passes 64 checks, including all 41 Boggle checks, and skips
ten optional id Tech 3 data checks. All 737 supplied installed files retain their
manifest hashes. The owner confirmed body/upward lettering, physical side letters
and full cube rotation work. Windows testing of the subsequent template UV
correction is pending. Exact whole-screen original matching, original
full/one-sided switching, specular lighting and dialog/pause appearance remain
unfinished; this pass does not claim complete Classic visual parity.

The focused cube suite and both supplied-data artwork/cube checkers also
pass ASan/UBSan, with leak detection disabled for the execution tracer. The preceding dummy
SDL software-backend run loads the complete installed board in both variants
and exercises the hand cursor, press/release, focus/leave and cursor cleanup.
