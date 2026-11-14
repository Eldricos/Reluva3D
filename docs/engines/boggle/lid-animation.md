# Classic settled-dice lid composition

Classic Original and Master now reveal and conceal the native dice over the
owner-supplied lid AVI. The playing camera and cube renderer are shared with
this transition view. No additional retail resources are imported.

## Observed composition

Read-only examination of the supplied executable's raise-lid camera segment
`0x42a215..0x42a446` and lower-lid segment `0x42a9a6..0x42abb5`
establishes the following source-video rectangles. Coordinates are at 640x480;
right and bottom are exclusive. Held frames retain the preceding rectangle.

| AVI frame | Original rectangle | Master rectangle |
| --- | --- | --- |
| 34 | 110,312–396,328 | 80,306–486,338 |
| 35 | 110,279–396,328 | 80,262–486,338 |
| 36 | 110,215–396,328 | 80,179–486,338 |
| 37–41 | 110,175–396,328 | 80,127–486,338 |
| 42 | 110,150–396,339 | 80,110–486,346 |
| 43 | 110,125–396,350 | 80,70–486,355 |
| 44 | 110,125–396,350 | 80,70–486,346 |
| 45 | 110,150–396,339 | 80,110–486,338 |
| 46–49 | 110,175–396,328 | 80,127–486,338 |
| 50 | 110,215–396,328 | 80,180–486,338 |
| 51 | 110,279–396,328 | 80,251–486,338 |

Opening overlays start at frame 34; closing overlays stop before frame 52.
The original clears the scene to yellow, renders dice, and calls the wrapper
at `0x45d9e0` on the AVI surface. That wrapper calls DirectDraw BltFast with
source-color-key flag 1. The source is the rendered scene surface, establishing
that dice are drawn over the video within the specified rectangle. Our renderer
uses a transparent temporary surface and copies only covered cube pixels in
that rectangle. It does not interpret yellow pixels in the AVI as transparent.

## Camera poses

| Pose | Original tilt / local position | Master tilt / local position |
| --- | --- | --- |
| Initial | -55.75 / (1.025, 0.4, -15.050009) | -54 / (0.8, -0.3, -13.199999) |
| Intermediate | -45.5 / (1.025, 0.25, -14.750009) | -45.75 / (0.8, -0.4, -13.199999) |
| Playing | -36.5 / (1.025, 0.125, -14.250009) | -34 / (0.8, -0.5, -13.199999) |

Frame 42 uses the intermediate pose and frame 43 the playing pose. Closing
frame 44 returns to the intermediate pose, and frame 45 to the initial pose.
Other visible transition frames use the initial pose. Pan, FOV and viewport
remain those documented in [camera projection](camera.md).

These facts were independently checked with an external, instrumented reference
harness. The native implementation selects a complete view from the AVI frame;
it does not accumulate transformations. Skipped frames, pause/resume and replay
therefore cannot lose camera steps. Round rules, input guards and the existing
100 ms video cadence are unchanged. The round timer starts after opening.

## Validation and limits

Invented-asset tests cover all 55 frames and an out-of-range frame for both
variants, assert untouched pixels outside the clip rectangle, check hidden
frames, and sample frames out of order. Metadata-only artwork checks also
render nine transition views per variant against installed resources. The
renderer preserves the board seed, Qu atlas and auto-orient choice.

The subsequent [complete cube renderer](cube-renderer.md) now supplies body
and lettering when the optional meshes are installed. Earlier imports retain
native flat faces. Original lighting and whole-screen VM appearance matching
remain unfinished. The
[roll-sequence correction](human-cursor.md#roll-sequence-correction) establishes
that the initial covered-tray shake is already supplied by the AVI. The owner subsequently confirmed that round end has no lid animation; native
results now retain the open board directly. Closing/reopening is used for
explicit Pause/Resume; see [pause presentation](pause-presentation.md).
Retail data, reference harnesses, disassembly and decoded previews remain
outside every Git worktree. Only independently written code and observations
are committed.

The full normal run passes 63 checks, including all 40 Boggle checks, with ten
optional id Tech 3 data checks skipped. The Boggle suites and supplied-artwork
renderer pass ASan/UBSan (leak detection disabled for the execution tracer).
The expanded artwork check renders 26 states across both variants and reports
`777705ff11b7c3dec6bc1557a9e37a59`; fresh-import verification agrees. All 737
installed files retain their manifest hashes. Native preview frames were
inspected outside Git. The owner subsequently confirmed that this transition build compiles and works.
