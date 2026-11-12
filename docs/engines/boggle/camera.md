# Classic playing-camera projection

Installed-artwork boards now use a native perspective camera with measured
Classic settings, rather than a homography fitted to tray-image corners.
Drawing, mouse hit testing, stored-path review and computer cursor destinations
use the same projected geometry. Original and Master have separate camera
poses. No new game resources or re-import are required.

## Behavioral contract

The game creates a 640x480 camera viewport. The rectangles previously captured
as `(0,0)-(640,350)` / `(0,0)-(640,355)` are draw regions, not the viewport's
projection dimensions. The initialization camera also changes during the last
two opening frames. Using the initial pose for the playing board would therefore
produce the wrong perspective.

| Playing setting | Original | Master |
| --- | ---: | ---: |
| Tilt, degrees | -36.5 | -34 |
| Pan, degrees | 0 | 0.25 |
| Local translation X | 1.025 | 0.8 |
| Local translation Y | 0.125 | -0.5 |
| Local translation Z, approximately | -14.250009 | -13.199999 |
| Horizontal FOV, degrees | 44 | 47 |

The initial poses are recorded in [the first presentation pass](presentation-fidelity.md).
At opening frame 42, Original adds 10.25 degrees of tilt and translation
`(0,-0.15,+0.3)`; frame 43 adds 9 degrees and `(0,-0.125,+0.5)`.
Master adds 8.25 degrees and `(0,-0.1,0)` at frame 42, then 11.75 degrees and
`(0,-0.1,0)` at frame 43. Pan and FOV stay unchanged. These observations establish
the final playing poses. The subsequent [lid composition pass](lid-animation.md)
uses these camera steps during opening and closing.

A local translation precedes camera yaw and tilt. Projection transforms each
world point into that camera frame, subtracts the local translation, then divides
horizontal/vertical coordinates by positive depth. Square pixels use focal
length `viewport_width / (2 * tan(FOV/2))` on both axes. Native output scales
640x480 to 800x600 by 5/4. Near-clipped or invalid inputs have zero reciprocal
depth. The fixed-point projection uses quantized Q30 basis/translation values
and Q16 camera coordinates; it avoids compiler-specific wide integers.

Cube centers form a centered grid at spacing 1.1. The native board keeps its own
row-major convention: columns increase world X and rows decrease world Y.
The original array's index convention is adapted when comparing its cube
positions; this is not a claim of matching the original shuffle or random seed.

The visible faces remain native flat quads on the grid plane with the existing
margins, color outlines and extrusion. Reciprocal camera depth also drives the
existing perspective-correct letter sampling. Retail mesh shapes, lighting,
rounded edges and roll physics are not implemented by this camera change.

## Evidence and independent validation

The supplied EXE has SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Camera creation at VA `0x44e081` supplies 640x480. Isolated observations of the
raise-lid segment `0x42a215..0x42a446` capture the adjustments at frames 42/43
for both variants. The initialization and cube-position checks from the prior
pass still provide 40 camera/profile/draw-region checks and 123 coordinate checks.

A separate external harness exercises projection in the supplied CD's
`DIRECTX/D3DRM.DLL`, SHA-256
`87683194d12694183bb38f15d21425297dc92854a2e5313f019e99e9e34529dd`.
It replaces OS services and provides synthetic device dimensions; viewport
construction/rendering stops before GPU work. It refreshes frame transform
state and calls the original frame/viewport math for points spread across both
playing views, replaying the initial pose and both opening adjustments. The
points include every native cube center and face corner in addition to a
synthetic 3D grid. The independently written native camera passes 2,438 screen-
coordinate checks with maximum error 0.997696 native output pixels, including
integer truncation, and 1,219 reciprocal-depth checks within three parts per
million. This validates projection math, not the complete Direct3D renderer,
Windows display/DPI behavior or pixel-identical original rasterization.

Only public API declarations were consulted from Wine's
[d3drm.h](https://github.com/wine-mirror/wine/blob/master/include/d3drm.h) and
[d3drmobj.h](https://github.com/wine-mirror/wine/blob/master/include/d3drmobj.h)
to identify COM method signatures. No Wine implementation is used. The native
camera is independently written from mathematical and behavioral contracts.
The engine never loads or executes the EXE or DirectX DLLs. Those files, raw
analysis, reference harnesses and decoded previews remain outside every Git
worktree; none is added to the import set.

Synthetic tests cover known 90-degree camera projections, yaw/tilt order,
translation, output scaling, reciprocal depth, near clipping, invalid parameters
and bounded extreme arithmetic. Existing tests check every cube center against
hit testing in both variants, Master last-row input, letter/Qu orientation,
results paths and visible/background computer behavior. Checksums were updated
only for affected installed-artwork views; prototype views retain their records.
Native previews of both supplied scenes were inspected outside Git. Original
whole-screen VM appearance, initial dice tumbling, mesh fidelity, exact dialogs
and pause audio remain comparison work.

The full normal run passes 63 checks, including all 40 Boggle checks with the
supplied install, and skips ten optional id Tech 3 data checks. The synthetic
Boggle suites and supplied-artwork renderer also pass ASan/UBSan with leak
detection disabled for the execution tracer. Install, CD and fresh-import
artwork checks for that playing-camera pass agreed on
`ac26f5b8390c5cb37eb467ea98241f0a` before transition views were added. All 737
supplied installed files retained their manifest hashes. The owner subsequently
confirmed that build works; Windows testing of the newer lid composition is pending.
