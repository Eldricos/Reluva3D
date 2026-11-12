# Classic presentation fidelity

The first pass replaces the computer's independently drawn arrow with installed
raised/pressed pointing-hand artwork. It uses the existing word-motion schedule;
it does not change difficulty, word choice, turn timing or scoring. Enable
Computer Cursor on Players with background play disabled to see it. Human
input and native menus continue to use the system pointer.

## Cursor resources

The existing imported `Language/English.dll` supplies RT_GROUP_CURSOR 5201
(raised) and 5200 (pressed), each with 1-, 4- and 8-bit indexed members. The
native reader selects the highest indexed depth, verifies every member and uses
its stored hotspot. Both bitmap planes are retained: output RGB is destination
AND mask, then XOR color. This preserves transparent and inverted pixels rather
than treating the bitmap as ordinary alpha artwork. Rows are decoded bottom-up.

The cursor is scaled by 5/4 with nearest-neighbor samples to match the native
800x600 view's scale relative to 640x480. Hotspots scale with it. This is a native
presentation policy, not a claim about original Windows display/DPI behavior.
The pressed image appears during the plan's existing cube hold intervals;
travel and release use the raised image. Submission/idle motion remains native.
The system pointer remains hidden only during visible computer turns and is
restored for pause, handoff, results and exits.

The DLL is read as data; it is never loaded or executed. No new import files or
re-import are needed. Minimal language bundles without either group retain the
native arrow; a partial or malformed enabled cursor bundle reports an error.
The reader supports only bounded, uncompressed 40-byte DIB headers with 1/4/8-bit
palettes, separate AND/XOR planes and valid hotspots. Unknown cursor encodings
are rejected explicitly.

Read-only evidence in the supplied EXE (SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`)
links initialization at VA `0x412827..0x41288f` to the two group IDs. Mouse-down
`0x4148aa` selects the pressed handle, while `0x414ad0` chooses raised/pressed
handles from input state. This establishes resource/state mapping, not a copy
of the original cursor implementation. The Windows AND/XOR bitmap layout is a
file-format contract; the decoder and compositor are independently written.

## Geometry evidence for the next pass

The first pass retained the projective tray fit. Isolated execution of the
Classic initialization segment captures these final settings, including when
replacement profile services return different values: the initializer overrides
those profile values. Forty camera/profile/viewport checks match.

| Setting | Original | Master |
| --- | ---: | ---: |
| Tilt, degrees | -55.75 | -54 |
| Pan, degrees | 0 | 0.25 |
| Position X | 1.025 | 0.8 |
| Position Y | 0.4 | -0.3 |
| Position Z, approximately | -15.050009 | -13.199999 |
| FOV, degrees | 44 | 47 |
| Stored viewport rectangle | (0,0)-(640,350) | (0,0)-(640,355) |

Cube-position service `0x428210`, with invented cube objects and a replacement
position sink, matches 123 coordinate checks over all 41 cells: centered rows
and columns at spacing 1.1, with the third coordinate zero. These measurements
do not establish the final camera transform, child-window origin, mesh rotation,
clip-plane conventions or animated cube poses. The existing `.x` resource reader
already parses the installed meshes, but a verified world-to-screen contract is
needed before replacing the tray fit. No mesh vertices are embedded in code.

## Validation and remaining work

An independent Pillow decode matches all 23 indexed cursor images byte-for-byte
in RGBA, with independently decoded AND masks and hotspots. All nine groups
validate. `reluva-tool boggle cursor-check <folder>` reports only counts and a
checksum; the supplied-install and fresh-import tests run it without exporting
assets. Its checksum is `286d1fe04293a3f50eadc1a22262ba65`.

Synthetic cases test 1/4/8-bit row padding, indexed colors, transparency,
inversion, opaque pixels, clipping, scaled hotspots, truncated planes, malformed
groups, exact language selection, partial bundles and the native fallback. A
scripted computer round loads an invented multi-resource PE and verifies that
the installed-cursor path restores pointer visibility at exit. Press/release
boundary tests also check the displayed cursor state.

The subsequent [playing-camera pass](camera.md) implements verified projection.
Moving cube overlays during lid clips, human cursor fidelity,
exact dialog placement/navigation and original pause/lid/audio appearance still
need comparison. This pass does not claim complete Classic visual fidelity.
All raw analysis, reference harnesses, decoded images and retail files remain
outside every Git worktree.

The full normal suite passes 63 checks, including 40 Boggle checks with the
supplied install, and skips ten optional id Tech 3 data checks. The CD and
installed copy have the same decoded cursor checksum. All 737 supplied
installed files retain their recorded hashes. Windows testing of this
presentation pass remains pending.
The synthetic Boggle suites and supplied-resource cursor checker also pass
AddressSanitizer/UndefinedBehaviorSanitizer, with leak detection disabled for
the execution tracer. Native raised/pressed cursor composites were inspected
from external preview files; no preview assets are committed.
