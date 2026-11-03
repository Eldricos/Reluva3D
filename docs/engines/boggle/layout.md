# Classic installed artwork

Classic Original and Master now use their installed scene, top player strip,
right-hand word panel and animated hourglass. The native framebuffer remains
800x600; installed 640x480 artwork is scaled to fit. The cube tops use the
installed Letters/LettersQu textures and a native approximation of the open
tray's perspective. Mouse hit testing uses those same projected polygons.

This is an intermediate presentation pass. Cube projection, lighting, rotation,
font, input box, buttons and digital-clock placement are native approximations;
this does not establish pixel matching with the original Direct3D screen.
The original toolbar, player-management dialogs and word-panel paging buttons
are not implemented. Page Up / Page Down still browse the submitted words.
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
and renders start, halfway and results states, reporting counts/checksums only.
The optional game-data and fresh-import checks run it against the supplied
resources outside Git. Captures, reference dumps and original media stay outside
all Git worktrees. The new presentation still needs the owner's Windows/VM
comparison; existing rules and music confirmations predate this pass.

The supplied install passes all 22 Boggle CTest checks, including the new
artwork and fresh-import checks. The normal full suite passes 33 checks and
skips 21 unconfigured optional data checks. Shared Video 1 tests also pass with
the Boggle engine disabled. All 737 original install files match their recorded
SHA-256 hashes after validation.
