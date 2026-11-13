# Classic cube geometry preparation

This stage prepares complete installed cube meshes for the next rendering pass.
It adds validation, normalization, directional letter-group identification and
optional imports. The subsequent [complete cube renderer](cube-renderer.md) now consumes these
resources. Original lighting and complete visual parity remain comparison work.

## Resource contracts

| Resource | Vertices | Polygons | Material groups | Uniform scale |
| --- | ---: | ---: | ---: | ---: |
| `Scripts/Rounded.x` | 470 | 108 | 7 | 0.13888895511627197 |
| `Scripts/Die.x` | 24 | 44 | 7 | 1 |

The [existing text .x reader](../../plans/boggle-1997/inspection.md) retains
polygon topology, per-corner normal indices, UVs and materials. `CubeGeometry`
validates the narrower cube subset and scales positions into game units.
Normals and UVs remain in template coordinates; no vertex data is embedded in
code. Bounds cap vertices/normals at 4,096, polygons at 1,024, corners per polygon
at 256 and material groups at 32. Nonfinite values, invalid references,
non-unit normals, out-of-range UVs/colors, transparent materials and unsupported
normalized bounds fail with an empty result.

Area-weighted polygon normals identify the six distinct letter-material groups
in the source mesh axes (+X, -X, +Y, -Y, +Z, -Z). The implementation uses Newell's
polygon area vector, so it does not assume the first three corners span a face.
Material ordering may differ between resources. Other groups and their complete
polygon/normal data remain present: they include the shoulders/body surrounding
the letter faces and must not be discarded by the next renderer.

## Evidence and rendering boundary

Source identities are recorded in the [install manifest](../../plans/boggle-1997/install-manifest.tsv).
Read-only examination of `Boggle.exe` identifies selection of `Rounded.x` versus
`Die.x` at `0x41d985..0x41d995`. The normalization segment at
`0x440e43..0x440e66` scales the rounded style uniformly. An external instrumented
reference run captures one scale call with all three components equal to the
value above; other styles make no scale call in that segment. These are measured
API/data contracts, not translated original implementation code.

The original also provides a one-sided representation at `0x441a90`, whose
helper at `0x440250` copies a selected mesh group. That capability alone does
not establish when Classic uses it. An external face-only native preview left
visible gaps around the rounded letter surfaces. It was rejected; no gameplay
renderer changes from that experiment are included. The future complete view
must render body and lettering together and verify face orientation, atlas
remapping, transforms, lighting, selection and hit testing consistently.
The precise Classic use of full versus one-sided visuals remains unverified.

The source has a rounded-style preference, but its default and VM appearance
remain comparison work. No new mesh-style setting is exposed in this stage.
The established [camera](camera.md), [lid composition](lid-animation.md) and
[human hand cursor](human-cursor.md) continue to use their tested rendering.

## Import and verification

New imports include either supported script when present. The full supplied
install now produces 55 selected files, 15,941,518 bytes, with import checksum
`8d14e075a0d60ad2d18085e311de6288`. Existing imports remain playable without these
optional scripts. Re-import before the later mesh-rendering pass if your game
library does not contain them. Retail files remain external to every Git
worktree; the native game never executes the supplied EXE or DirectX DLLs.

```
reluva-tool boggle cube-check /external/Boggle
```

The command validates every present supported cube script and reports counts
and a checksum only. It exports no geometry or images. Its digest covers scaled
positions, normals, UVs, topology, material properties and directional groups,
with numeric metadata rounded to one millionth in canonical little-endian form.
Missing optional scripts produce a zero-mesh report; malformed present scripts
fail. Both supplied copies report two meshes, 494 vertices and 152 polygons,
checksum `e985c393b66b88890345759e0ab219fc`.

Invented-geometry tests cover normalization, non-file-order letter groups, body
retention, unchanged source data and malformed indices, materials, normals,
UVs, scale and degenerate geometry. Import tests check both optional scripts.
The optional supplied-artwork and fresh-import checks now also run `cube-check`.
Their metadata records add cube checksums while retaining the existing artwork
checksum `777705ff11b7c3dec6bc1557a9e37a59` and gameplay records.

The full normal run passes 63 checks, including all 40 Boggle checks, with ten
optional id Tech 3 data checks skipped. Geometry/import unit tests and the
supplied cube validator also pass ASan/UBSan (leak detection disabled for the
execution tracer). Install and CD cube/import digests agree, and fresh-import
validation retains the same cube digest. All 737 installed files retain their
manifest hashes. The owner subsequently confirmed that the preparation build compiles.
