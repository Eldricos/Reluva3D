# Classic installed artwork

Classic Original and Master now use their installed scene, top player strip,
right-hand word panel and animated hourglass. The native framebuffer remains
800x600; installed 640x480 artwork is scaled to fit. The cube tops use the
installed Letters/LettersQu textures and a native approximation of the open
tray's perspective. A single projective plane maps the grid onto the measured
tray corners. Letter textures use reciprocal-depth interpolation across each
cube top; mouse hit testing uses those same projected polygons.

This is an intermediate presentation pass. Cube projection, lighting,
font, input box, buttons and digital-clock placement are native approximations;
this does not establish pixel matching with the original Direct3D screen.
The optional [handwriting font](fonts.md) now renders submitted words.
The original toolbar and full player-management dialogs are not implemented.
A [native one-human name editor](players.md) now supplies the round headers.
[Word-panel history controls](history.md) now provide clickable arrows, page
keys and mouse-wheel browsing.
The panel labels WORDS and SCORE describe native counters; they do not claim
to reproduce the original Found/Total labels' semantics.

## Resources and fallback

Each variant uses three additional files:

| Variant | Word panel | Name strip | Hourglass |
| --- | --- | --- | --- |
| Original | `Bitmaps/ClsO1W.bmp` | `Bitmaps/ClsO1N.bmp` | `Anims/ClsO1Tim.avi` |
| Master | `Bitmaps/ClsM1W.bmp` | `Bitmaps/ClsM1N.bmp` | `Anims/ClsM1Tim.avi` |

Both panels are 143x480 and both strips 497x32. Original's timer has 38 frames
at 68x128; Master's has 39 at 80x140. Both are indexed 8-bit Microsoft Video 1.
The open and closed scenes are frames 43 and 54 of the existing respective
55-frame `All.avi` clip. Static scenes still require that clip when round
animations are disabled. Timer sprites use a black color key.

New imports include these six files when present. Re-import from the full
install or expanded CD to obtain them. Earlier copies without all three files
for a variant keep the previous prototype view. A partially present bundle or
malformed artwork reports an error. Disable **Classic installed artwork**
(`classic_artwork=0`) to select the prototype explicitly. This setting is
independent of round animations, sound effects, music and frontend movies.

## Evidence

Source identities are recorded in the [install manifest](../../plans/boggle-1997/install-manifest.tsv).
The inspected executable SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Read-only binary inspection established the Classic resource families and
layout dimensions; no original implementation or disassembly text is included
in the native code.

The Classic creation routine at `0x428790` selects Original versus Master by
minimum word length. Its timer rectangles are `(30,98)-(98,226)` near
`0x428fed` and `(0,95)-(80,235)` near `0x429089`. Resource construction near
`0x429013` / `0x4290b1` selects the matching `Tim.avi` suffix.
`CClassicTimer::SetTime` at `0x42bde0`, particularly `0x42be2c..0x42be4d`,
selects 36 elapsed-time buckets, decrementing the nonzero bucket index.
The native timer follows that integer calculation rather than playing all
38/39 frames at the AVI's nominal 30 fps. Numeric remaining time uses whole
seconds, as observed near `0x42bf95..0x42bff5`.

English dialog IDs 250/350, 260/360 and 261/361 establish the main area,
word panel and four-slot player strip. Native rendering uses only one player.
Measured source geometry and these observations guide the layout; exact font,
dialog-unit conversion and original camera geometry remain unverified.

## Validation

All 77 timer frames match the external FFmpeg 7.1.5 decoder byte for byte in
RGBA. No external decoder code or decoded frames enter the repository.
Synthetic tests cover timer boundaries, both board sizes, projected hit testing,
selected cells, pending words, results, untimed play, older imports and malformed
bundles. A scripted Master round selects four last-row cubes, submits a word,
ends and replays the round. Checksum records contain only invented fixtures.

`reluva-tool boggle artwork-check <folder>` validates both available bundles
and renders aligned start, halfway, results and quarter-turned start states,
reporting counts/checksums only.
The optional game-data and fresh-import checks run it against the supplied
resources outside Git. Captures, reference dumps and original media stay outside
all Git worktrees. The owner confirmed the installed-artwork pass works on Windows. Exact
appearance still needs VM comparison. The subsequent projective-plane/texture
refinement was subsequently confirmed working by the owner on Windows.

The supplied install passes all 25 Boggle CTest checks, including the new
artwork and fresh-import checks. The normal full suite passes 36 checks and
skips 22 unconfigured optional data checks. Shared Video 1 tests also pass with
the Boggle engine disabled. All 737 original install files match their recorded
SHA-256 hashes after validation.

## Perspective refinement

The initial renderer used bilinear grid placement and affine interpolation of
texture coordinates. A tilted rectangular plane needs a projective mapping:
its equally spaced rows converge toward a vanishing point. The current native
renderer fits one homography to the same measured tray corners and interpolates
texture coordinates with reciprocal depth. This changes the rows and letter
mapping while retaining the earlier corner calibration and hit-test polygons.
It does not establish the original camera parameters, orientation generator or lighting.
No additional assets or import changes are required.

The projection uses bounded integer homogeneous coordinates and integer
reciprocal depth. An invented trapezoid verifies the intersection of diagonals
at its analytically known projective center; degenerate, crossed and out-of-range
inputs are rejected. Gradient-texture records cover both boards, selected cells
and the separate Qu atlas. The existing Master last-row mouse/replay test follows
the new projected positions. This is original mathematical implementation;
no external renderer code was consulted or copied.

The layout tests and actual-resource artwork checker also pass AddressSanitizer
and UndefinedBehaviorSanitizer (LeakSanitizer disabled for the execution tracer).

## Auto-orientation

The [Auto-orient cubes setting](options.md#cube-orientation) now controls each
cube's texture orientation in both renderers. Its behavior follows the owner's
VM observation; the assignment of quarter turns is independent native code.
Textures rotate in their own face coordinates before perspective projection.
The same inverse sample transform handles upright, clockwise quarter turn,
half turn and counterclockwise quarter turn, including the separate Qu atlas.

Synthetic asymmetric textures check all four orientations. Scripted rounds in
both variants and both renderers compare the word/score/time panels with the
setting enabled and disabled, and verify stable replay and changed New Board
orientations. The existing aligned rendering records remain unchanged. New
records add rotated states made only from invented fixtures. Actual-resource
checks now also render the quarter-turned view without exporting images.

The owner confirmed the orientation update works and clarified that cubes are
randomly oriented at round start with auto-orient disabled; players cannot
rotate them manually. The current implementation follows that interaction.

The owner confirmed the handwriting update works. History controls now reserve
the area below 23 rows for arrows and a visible range; exact original visible
capacity and other UI details remain unverified.
