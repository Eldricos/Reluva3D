# Classic physical die sides and orientation

Generated Classic boards retain each source die's six normalized faces, its
source index and the selected upward side. No extra random draws are made:
shuffle order and upward letters remain identical for the same native seed.
Copies and Replay preserve this metadata. `Board::set` clears it; injected
boards display only the supplied top letter and never invent physical sides.
Invalid unused faces are left blank; selected faces retain existing validation.

## Observed contracts

Read-only executable analysis and an external isolated execution probe establish
the following behavior. Original executable SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
No original instructions, resource vertices, strings or probe binaries are
included in the implementation or fixtures.

The six normal initializations at `0x44053d..0x440690` identify these source axes:

| Face-input column | Outward source normal |
| --- | --- |
| 0 | -Y |
| 1 | +X |
| 2 | +Z |
| 3 | -Z |
| 4 | -X |
| 5 | +Y |

The isolated assignments at `0x441502..0x44159d`, with fabricated string and
face-setter services, bind the six ordered string inputs to face IDs 0 through
5. All six checks pass. Directional material groups are identified from mesh
geometry, rather than assumed material numbers.

The settled-pose method at `0x442240` was exercised for both mesh styles, all six
upward sides, auto-orient on/off and four extra turns: 96 cases. Chosen normals
point toward world -Z, and the method preserves cube position. The independently
constructed native matrices match all 96 results (converting the original
positive-Z extra-turn convention to the native clockwise convention).

Native code constructs orthonormal frames from the selected normal, an observed
back direction and their cross product. It does not reproduce the original Euler
instruction sequence. Back points source -Z for inputs 0/5 and +Y otherwise;
angular inputs 2/4 use -Y. This gives the measured angular/rounded difference.
Extra native clockwise turns rotate around world Z by negative quarter turns.
The original random turn assignment and shuffle are not being emulated.

## Rendering and interaction

Each letter surface receives its corresponding retained face, with the distinct
Qu atlas wherever needed. Upward UV axes are aligned from the mesh's UV tangents
before the optional whole-cube spin. Other sides retain source-template UVs;
matching the original side bitmap orientation remains unverified. Normals rotate
with the body before native diffuse lighting. Picking projects the same rotated
triangles, and human input, computer cursor targets and review markers use the
same upward-face centers. Centers refresh on New Board and remain stable on Replay.
Missing meshes still use the previous flat renderer and its existing policy.

## Validation

Synthetic tests check orthonormal poses, selected normals, four-turn closure,
style differences, real side lettering, Qu, upright top gradients and picking
for both variants/styles with auto-orient on/off. Generation checks compare the
previous random-draw contract across 256 Original/Master seeded boards. Existing
mouse-entry/scoring, animation masks and prototype compatibility remain covered.
External supplied-resource previews check both meshes and variants without
adding assets to Git. The artwork digest becomes
`def67430fb55182fb2d3fc6f5c6aa3b2`; geometry and import contents are unchanged.
