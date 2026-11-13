# Classic cube template texture mapping

The owner confirmed physical die sides and full cube rotation work. A subsequent
read-only check found a missing loader correction: four rounded-die faces need
a horizontal texture mirror. Native rendering now applies that correction before
deriving the upward glyph's axes. Angular templates retain their existing UVs.

## Measured contract

Original executable identity is recorded in [die orientation](die-orientation.md).
The loader segment `0x440d4b..0x440e26` adjusts group texture coordinates after
matching a group to its source face normal. Faces here use the measured six-input
order, independent of a mesh's material numbers:

| Face input | Source normal | Angular U | Rounded U |
| --- | --- | --- | --- |
| 0 | -Y | u | u |
| 1 | +X | u | 1-u |
| 2 | +Z | u | 1-u |
| 3 | -Z | u | u |
| 4 | -X | u | 1-u |
| 5 | +Y | u | 1-u |

After that optional mirror, the observed initial atlas mapping is
`U = corrected_u / 6`, `V = (5 + source_v) / 6` in retained-mode texture space.
For the native image's top-left origin, that base tile corresponds to local
`(corrected_u, 1-source_v)`. The existing letter-index tile selection and Qu
atlas handling remain unchanged. This establishes a loader data transform;
it does not establish identical pixel filtering or edge rounding.

An external isolated probe executes that segment using invented mesh vertices,
group buffers and a capture-only mesh-update service. It exercises angular and
rounded styles, all six matched faces and three asymmetric UV pairs per face:
36 vertex comparisons pass. The additional intermediate upload occurs only for
the four mirrored rounded faces. Probe code, retail binaries and raw research
remain outside every Git worktree. The implementation expresses the measured mapping rule in independently written
native code, with no original instruction sequence.

## Native integration and verification

`CubeRenderer::prepare` corrects private render UVs; parsed resources and
`CubeGeometry` remain unchanged. Upward UV tangents are derived after correction,
so auto-orient continues to align top letters, and turning it off still rotates
the body and lettering together. Picking, centers, board generation and rules
are unaffected. Bodies without a letter group are never mirrored by this rule.

A synthetic asymmetric atlas verifies the visible front-side gradient changes
handedness for the rounded style while retaining the angular result. Existing
all-face top-gradient, Qu, pose, mouse-entry/scoring, clipping and rotated-picking
tests continue to pass. External previews of both variants/styles show changed
rounded side lettering and byte-identical angular images; no images enter Git.
The supplied-artwork checker renders 26 states and reports
`ff08f12fd6fbda7487ff2e8702b60178`. Cube geometry still reports
`e985c393b66b88890345759e0ab219fc`, and selected import files are unchanged.

Full normal regression and focused ASan/UBSan renderer/resource checks are used
for this stage. Exact original filtering, lighting, full/one-sided switching,
seeded shuffle and whole-screen appearance remain separate comparison work.
