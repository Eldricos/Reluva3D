# Game selection and Classic setup

The engine now plays the available intro, then opens game selection before
Classic. Escape skips the intro; see [movie playback](movies.md). When present,
`Bitmaps/Select1.bmp` supplies the installed game's 640x480 background. Controls use native behavior and styling, with installed mode placement when
available: choose Classic, then Original or Master, the round length,
then choose the human player name on Players and choose Play. Play starts the existing roll/opening sequence immediately; it does
not require another Enter on the round's Ready screen. Other game modes are
shown as unavailable and cannot be started.

Keyboard controls:

- Selection: Enter, keypad Enter or C opens Classic. Escape does nothing.
  The native Launcher button / L returns to the launcher.
- Version/setup: 1 selects Original; 2 selects Master. Left/Right adjust the timed
  duration in 30-second steps, bounded to 1:00..5:00. T toggles no timer and
  restores the last timed duration. Enter/keypad Enter advances to Players.
  Escape or Main Menu returns to selection.
- Players: shows the current human name, chosen version, duration and local
  roster/turn controls. Options / O opens the installed Classic options panel
  when available. Change Name / N opens the native name editor; save or cancel returns
  to Players. Enter/keypad Enter plays. Escape or Main Menu returns directly to
  selection, rather than to version selection.
- During a menu-launched game, Escape does nothing. The explicit native Menu
  button / F3 returns to selection. Window close and Return to Launcher requests
  leave the engine directly. Replay and New Board retain their existing behavior.

Mouse controls provide the same choices. Selection changes, Back and Play do
not consume another navigation action from the same input batch. Pending round
input is discarded before menus become interactive again; global quit requests
still take effect. Text entry is active only in the name editor or round, and audio output is
released before returning to menus or changing graphics modes.

The setup starts from the launcher's saved Classic options. Menu changes apply
for this engine session and are preserved on return from a round; they do not
write the launcher configuration or original Windows registry. Supported
local sessions use up to four humans/computers in U.S. English; see
[Traditional play](traditional.md) and [Chess Timer](chess.md). Sound effects and music still
use the launcher's settings. A random music choice is made once per engine
launch, including across menu re-entry. Returning to menus stops the round's
music; entering Classic again restarts that same selected loop. Replay within
Classic continues its existing music cursor. These are explicit native policies;
exact original menu audio still needs VM comparison. Launcher/L and Menu/F3
are native convenience controls, not claimed original shortcuts.

**Game selection menu**, enabled by default in launcher options, is saved as
`classic_menu=1`. Disable it (`0`) to use the previous direct Classic Ready screen
and Escape-to-launcher behavior. This also allows direct regression comparisons.

## Installed mode-button placement

When Classic installed artwork is enabled, game selection reads exact RT_DIALOG
10813 (`0x2a3d`) / language 0x0409 from the existing English DLL. Five static
controls bind by ID 15001..15005 in Classic, Breakaway, Battle, Space and In Your
Face order. Control enumeration order does not determine the binding. The
selection caller loads this template at `0x43adfc..0x43ae10`, as recorded below.
The DLL and executable hashes identify the supplied binaries used for this
read-only analysis.

The independently written consumer maps each axis of the template client area
to the existing 640x480 selection canvas. Drawing and Classic mouse picking share
the resulting rectangles. Required IDs must be unique static controls, inside
the client bounds, ordered without overlap, large enough for native labels and
clear of the native title/launcher controls. A present malformed selected layout
reports an error before intro playback. Missing templates or disabled installed
artwork keep the previous native mode placement; existing imports need no new
files. Resource text, retail coordinates and bitmap bytes are not embedded in
source or fixtures.

Available button clips fit inside each mode rectangle with their square aspect
ratio retained. The native nearest-neighbor rasterizer clips writes to the screen;
movie timing and the existing looping policy are unchanged. Labels and unavailable
mode notices stay inside their own rectangles, including when movies are absent
or disabled. Only Classic can launch. Enter/C, inactive Escape on selection,
native Launcher/L, version/Players navigation and menu-return input guards retain
their existing behavior. Other resource controls, network/chat, original font
metrics, skinning and original hover-trigger behavior are outside this pass.

This is a native canvas mapping, not Windows font-dependent dialog-unit
conversion or a claim of complete original frontend fidelity. Later Players and
version setup screens retain their existing native layouts. The player-name
editor now has its own installed-template consumer; see below.

## Installed player-name editor

With installed artwork enabled, Change Name uses exact RT_DIALOG 1600 /
language 0x0409 from the existing English DLL. The name field binds to edit
control 1601, Save to button 1 and the prompt to static control 65535. The
original constructor requests template 1600 at `0x445461`; the edit binding
uses control 1601 at `0x445508`. The executable and DLL identities are recorded
below. This is read-only layout evidence, not a reconstruction of the original
registry/profile behavior.

The independently written consumer centers the client area in the 640x480 view
at two native pixels per template unit. It validates unique required IDs,
classes, bounds, capacity for the native 18-character field, vertical separation
and room for the native Cancel action. Missing templates or disabled installed
artwork retain the previous editor and mouse targets. A present malformed
selected layout fails explicitly. Control rectangles come from runtime data;
retail resources and captions are not embedded in source or fixtures.

Save and field drawing/picking share those rectangles. Native Cancel remains
available to the left of Save; help and validation errors use the bottom footer,
clear of the setup controls. Enter/Save, Escape/Cancel, Ctrl+A, field-click
selection, text entry, deletion, trimming and the 18-printable-ASCII-character
limit retain their existing behavior. Save/Cancel consume their navigation batch
so queued Enter cannot start a round. Text input stops when the editor closes.
Profile persistence, name history, round/high-score naming and safe save-directory
rules are unchanged; no re-import or save migration is required.

The original small name-entry template supplies relative placement. Native
fonts, colors, wording, footer and added Cancel remain explicit presentation
policies. This does not claim original Windows metrics, skins or name-storage
interoperability; the surrounding version/Players screens remain native UI.

## Installed local-player roster

With installed artwork enabled, Local Players uses exact RT_DIALOG 10000 /
language 0x0409. The four selector faces bind to combo controls 4107..4110,
the message area to static control 1344, Done to button 1 and Cancel to
button 2. The original constructor selects this template at `0x44b1c7`; its
Classic caller constructs it at `0x4192e0`. Combo bindings appear at
`0x44b602..0x44b645`, with buttons/message at `0x44b58a..0x44b5b5`.
The executable/DLL hashes below identify the read-only source of these facts.

The native consumer uniformly fits the client area within 608x400 pixels of the
640x480 view, with a maximum two-pixel unit scale. Required controls must have
unique IDs, expected classes, readable dimensions and separated message, fields
and actions. A combo's template height includes its dropdown list, which may
extend beyond the dialog client. The consumer bounds that metadata and validates
a native collapsed face of 14 template units inside the client; it does not
mistake the full popup height for an out-of-bounds ordinary control.

Drawing and mouse picking share the four collapsed rectangles and Done/Cancel
rectangles. Long names use native ellipses on the compact faces; the existing
full-width dropdown shows complete names and difficulty labels, with its existing
scrolling and keyboard controls. All four human/computer selectors, optional empty
slots and roster compaction retain their existing rules. A human first-slot
choice updates the active profile only when Done succeeds.
Native player labels replace portrait artwork; editable Windows combo widgets
and original popup metrics/skins are not reconstructed in this pass.

The owner confirmed in the original VM that Cancel discards all player-dialog
changes. Done/Enter applies the draft choices; Cancel/Escape restores the names
and human/computer types present when the roster opened. This applies to both
installed and fallback layouts. The native dropdown retains its separate Escape
handling while open. Closing consumes the remaining input batch, so a queued
Enter cannot start a game.

First-slot human selection stays provisional: profile/history writes are deferred
until Done. Cancel and quitting during editing leave the saved active name and
history untouched. If Done cannot save the profile, the roster stays open with
an error and the draft intact; the user can retry Done or cancel safely. The
roster itself remains session state. Missing templates or disabled artwork keep
the previous geometry with the same Done/Cancel behavior;
a present malformed selected layout fails explicitly. No new imports or save
migration are required. Source and fixtures contain no retail resource bytes,
portraits, text or copied control rectangles.

## Classic options panel

With installed artwork enabled and the template available, Players now exposes
native Options / O. The panel selects exact RT_DIALOG 10834 / language 0x0409;
the original Classic property-page construction requests it at `0x42b807` and
`0x42b8ce`. The related 10823 template omits background play and is not selected
for this full native panel. The executable/DLL identities below identify the
read-only source of these layout observations.

Required controls bind by ID: computer cursor 1312, background play 1313,
auto-orient 1311, Chess Timer 1299, Traditional 1300, round-length trackbar 1907
and No Timer 15303. The bounded standard/extended reader supplies the rectangles.
The consumer requires unique IDs, button classes or the named trackbar class,
readable dimensions, in-client bounds and non-overlap. The page is centered at
two native pixels per template unit. Native Done/Cancel buttons, labels, colors,
fonts and footer surround this property-page layout; the original Windows
property-sheet frame, group boxes and skin are not reproduced.

Click/drag the length ruler or use Left/Right to choose 1:00..5:00 in 30-second steps.
T / No Timer toggles timing and remembers the last timed length. The ruler and
Left/Right are inactive while No Timer is on. A toggles auto-orient; C selects
Chess Timer; R selects Traditional; B toggles available background play; V toggles
available computer cursor movement. Computer controls use the same roster-based
availability rules as the existing Players controls. Background play disables
cursor movement without clearing its stored choice.

The panel edits session options provisionally. Done/Enter applies them;
Cancel/Escape restores both the options and remembered timed length from panel
entry. Each recognized action consumes its navigation batch; ordinary mouse
movement does not swallow a subsequent click. Applying or cancelling cannot
also start a round from queued Enter input. Applied values feed the next Classic
round, including auto-orient, and persist through menu re-entry for this engine
session. They do not change launcher configuration, original registry data or
retail files. No new save format, files or re-import are required.

If the template is missing or installed artwork is disabled, the prior setup
controls remain available and the new Options entry is omitted. A malformed
present selected template reports an explicit error. This native setup panel
uses installed relative placement; exact Windows trackbar/capture behavior,
original modal timing and original settings persistence are not claimed.

## Round-length ruler dragging

The native options ruler now supports holding the left button and dragging.
Press, motion and final release use the same bounded nine-position duration
mapping as a direct click. Drag samples coalesced into one polling batch all
apply, including the final release position; later pointer motion cannot keep
changing the duration after release. Motion beyond the track clamps to 1:00 or
5:00. These are native input policies, not execution of original trackbar code.

Leaving the window or losing focus stops the drag and retains its last draft
value. Options input is ignored while unfocused, and a focus-regain batch cannot
restart a drag or apply a queued confirmation. A fresh press is required. No
Timer also stops dragging and disables further ruler changes. Done/Cancel stop
dragging; Cancel restores both options and the remembered timed length as before.
Reopening starts without a held drag. The frontend does not add OS mouse capture
or claim matching the original Windows capture behavior outside its window.

## Evidence and missing-file correction

The source EXE SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
These are static observations from the owner's binaries; addresses are virtual
addresses unless described as file offsets. No original code is executed by the
native engine, and no disassembly or retail UI text is included in Git.

- The intro routine at `0x406ac0` requests `Anims/Intro.avi` at `0x406ad9`.
  Its failure path at `0x406af7` selects runtime class descriptor `0x4763c0`.
  `CGameSelect` returns that same descriptor at `0x43a7a0`.
- `CGameSelect::Create`, starting at `0x43aae0`, loads `Palettes/GS1Pal.pal`
  at `0x43ab76` for 8-bit display. Both display branches load
  `Bitmaps/Select1.bmp` at `0x43ad9f` / `0x43adb0`. The native true-color BMP
  reader uses the file's own palette and produces RGBA.
- The selection dialog is resource `0x2a3d` in the owner's English DLL,
  SHA-256 `9922ca1656fa8a9f376b79cdc3c1a4a71bd0d75f01156e07e4c3a126304628fe`.
  Its file offset is 18,016 and size 320 bytes. It contains five mode controls
  with IDs 15001..15005, then two further controls 15006..15007, plus chat
  controls. Code selects and loads it at `0x43adfc`..`0x43ae10`. The five mode slots correspond to
  the existing language string IDs 11001..11005. Native controls cover only
  Classic; this does not implement the dialog's full player/network behavior.
- The installed selection background SHA-256 is
  `90659f541ac4e2eb13c2740222b7839a809adb8959d314f09c6e0094404b556b`.
  Both supplied expanded trees contain it, at 640x480 / 8-bit BMP.
- `INTRO.AVI` and `ClsBut8.avi` declare `cvid` / 24-bit Cinepak in their AVI
  stream formats. Their names do not establish a Video 1 or 8-bit codec.
  The existing Video 1 reader is therefore not used to decode these files.
- The `CTitleSequence` methods request `bitmaps/hasbro1.bmp` or `hasbro3.bmp`
  at `0x4549d5` / `0x454a1c`, then `title1.bmp` or `title3.bmp` at
  `0x4546d4` / `0x454716`, depending on display depth. The bitmap loader
  `0x4622d0` opens a filename and returns null on open failure at `0x4622ed`.
  These calls are not a proven embedded-bitmap fallback. All four files are
  absent from the supplied expanded trees. The owner reports an Intro-video-to-main-menu flow. The native engine uses
  that available AVI path; these absent bitmap files do not gate it. The
  separate title-bitmap path remains unreconstructed.
- The earlier outstanding-evidence list incorrectly included `GS1CS.bmp` among
  missing files. It is present in both original manifests and copies, at
  300x200; SHA-256 `c3a90f5dabe13f87182b18d5ed290dbbede8a4f8a37d05b5b0cdf7db1c22debf`.
  References include `0x411f08`, `0x427cef` and `0x427d1f`. That uncertainty
  is corrected rather than treating the file as absent.

Private inspection decoded the dialog metadata and viewed the supplied bitmap
and media headers. Raw dialogs, text, contact sheets, screenshots and analysis
stay outside every Git worktree. The [manifests](../../plans/boggle-1997/inspection.md)
identify the original files. No additional external implementation was consulted.

## Scope and validation

This is the first frontend navigation pass, not a recreation of the original
frontend or Classic board layout. The intro and available mode buttons now use the independent
Cinepak reader. Full player configuration, original dialogs, fonts, original cursor/menu
sounds and original Classic overlays/timer/results rendering remain unfinished.
The owner confirmed this original VM sequence: Intro video (Escape skips to
main menu) -> Select Game Type (Escape does nothing) -> Select Version
(Original/Master; Escape returns to main menu) -> Players (Escape returns to
main menu) -> Game (Escape does nothing). The native frontend now follows the
confirmed post-intro stage order and Escape behavior. Intro skipping is now
implemented. The native setup/Players layout, one-player confirmation and
additional shortcuts remain implementation choices, not original UI claims.
Escape also leaves native results visible; the owner's description did not
separately establish Escape behavior on the original results screen.

The background is optional so existing 39-file imports continue to work using
a native selection background. A present but malformed image or wrong
640x480 dimensions produce an explicit error. New imports include the image,
the publisher movie and five available button clips, containing 46 files for the supplied release.
See [import](import.md).

Synthetic records cover selection, version, Players, Master/Original play, scoring, results,
return and re-entry using invented BMP pixels and data. Additional checks cover
mouse input, repeat and input-batch guards, unavailable modes, window/launcher
exit, delayed menu time, exact round expiry, invalid media/settings, old imports
without artwork, and random music/audio lifetime across menu re-entry. Existing
direct-round records remain unchanged. A one-pixel layout mutation made the
new frontend record fail; the restored layout passed again.

`reluva-tool boggle frontend-check <folder>` validates the optional background
and reports only counts plus MD5 of decoded RGBA. Both supplied copies report
one image / 307,200 pixels, checksum `60afd78d7288730155ffff7dda912a9b`.
The optional frontend game-data step and the extended import step record only
metadata, with raw output and any copied assets outside Git.

All 19 Boggle CTest checks pass with the supplied install; CD import and frontend
checks match the same metadata. The full ordinary CTest run has 31 passing checks
and 19 optional data checks skipped without configured game paths. Boggle tests
and the supplied background decoder pass ASan/UBSan (LeakSanitizer disabled for
the execution tracer). An external SDL dummy-video/audio run from the fresh
40-file CD import traversed selection, Master play, menu return, Original play
and exit with nonzero output audio. Private captures were inspected locally.
These software checks do not establish Windows playback or original UI parity.
All 737 original install files remain unchanged against their SHA-256 manifest.

After the owner's VM report, synthetic navigation checks explicitly verify inactive
Escape on the main menu and in-game, Escape-to-main from both version selection
and Players, the Players confirmation gate, and explicit native Menu/F3 exits.
The existing direct-entry option retains its earlier Escape-to-launcher policy.
The revised flow passes all 19 Boggle checks and ASan/UBSan; the ordinary full
suite remains 31 passing checks with 19 optional data checks skipped. A new
external SDL run traversed both Players confirmations, reached live Master and
Original boards, ignored Escape in-game, and exited through the explicit native
controls with nonzero audio. No claim of full original player-setup UI is made.

The [native player name](players.md) is now editable and remembered separately
from original data. Full original player configuration remains unfinished.

The installed-selection pass adds invented standard/extended templates with
reversed control order, exact-language/missing fallback and malformed required
controls. Scripted navigation verifies the installed Classic target, retired-target
rejection, Master setup/play, menu return, unavailable modes and artwork opt-out.
Synthetic Cinepak clips verify fitting, aspect ratio, animation timing, clipping
and extreme off-screen rectangles. Existing fallback frontend records remain
unchanged. External SDL software-backend runs inspect supplied animated selection
and exercise Classic picking, both versions, game entry and menu return. Retail
captures and the preview harness remain outside Git.

All 43 Boggle CTest checks pass with the supplied install. The full suite passes
66 checks and skips 10 optional idtech3 data checks. Frontend, dialog and import
suites pass ASan/UBSan with LeakSanitizer disabled for the execution tracer.
All 737 installed files retain their manifest hashes. Windows owner testing of
the installed selection layout is pending.

Name-editor fixtures cover standard/extended templates, exact-language/missing
fallback, selected-state rendering, footer clearance and malformed required
controls. Scripted editing checks 18-character input, obsolete-target rejection,
field-click replacement, mouse Save/Cancel, keyboard Save, blank-name errors,
retry after editing, batch guards and persisted history without cancelled names.
Existing fallback player/frontend records remain unchanged. External SDL
software-backend runs inspect the supplied editor and exercise field selection,
Save, Cancel, persistence and text-input cleanup in both versions. Captures and
the preview harness remain outside Git.

For the name-editor stage, all 43 Boggle CTest checks pass with the supplied
install; the full suite passes 66 checks and skips 10 optional idtech3 data
checks. Frontend, player and dialog suites pass ASan/UBSan with LeakSanitizer
disabled for the execution tracer. All 737 installed files retain their manifest
hashes. Windows owner testing of the installed name-editor layout is pending.

Roster fixtures cover both template encodings, popup heights extending beyond
the client, exact-language/missing fallback, malformed classes/IDs and overlapping
fields/actions. Scripted selection exercises all four installed mouse targets,
humans, both ends of the computer list, clearing/restoring optional slots,
Done/Cancel/Escape, reopen preservation, profile history, opt-out and queued-launch
guards. Existing fallback records remain unchanged. External SDL software-backend
runs inspect the supplied compact roster, choose humans/computers, reopen it,
enter Classic and return to menus in both versions. Captures and the preview
harness remain outside Git.

For the roster-layout stage, all 43 Boggle CTest checks pass with the supplied
install; the full suite passes 66 checks and skips 10 optional idtech3 data
checks. Frontend, player and Traditional suites pass ASan/UBSan with LeakSanitizer
disabled for the execution tracer. All 737 installed files retain their manifest
hashes. Windows owner testing of the installed roster layout is pending.

The Cancel correction additionally tests all-four-slot rollback, first-slot
human/computer changes, reopening, unchanged saved profile/history during drafts,
failed Done followed by Cancel, and successful commit with a consumed input
batch. Cases cover standard/extended templates and fallback geometry. Seven
synthetic roster/dropdown frames change for the visible Cancel control and draft
profile behavior; gameplay result frames remain unchanged. External SDL runs
change all four selections, cancel and compare the reopened roster with its
initial pixels in both versions. No original save files are written.

The owner-confirmed Cancel correction passes all 43 Boggle CTest checks with the
supplied install; the full suite passes 66 checks and skips 10 optional idtech3
data checks. Frontend, player and Traditional suites pass ASan/UBSan with
LeakSanitizer disabled for the execution tracer. All 737 installed files retain
their manifest hashes. Windows owner testing of the correction is pending.

Options fixtures test standard/extended templates, named trackbar classes, exact
language, all nine time steps, extreme click coordinates and malformed/overlapping
controls. Scripted editing covers disabled controls, mouse movement plus clicks,
No Timer, remembered duration, Done/Cancel, computer/background interactions,
menu return and input-batch guards. Original/Master rounds record the applied
2:30 duration and correct score. Existing fallback checksums remain unchanged.
External SDL software-backend runs inspect supplied options, enter oriented-off
Chess with No Timer, then verify the saved session choices and remembered 2:30
length after menu return. Retail captures and preview code remain outside Git.

For the options-panel stage, all 43 Boggle CTest checks pass with the supplied
install; the full suite passes 66 checks and skips 10 optional idtech3 data
checks. Frontend, options and seed suites pass ASan/UBSan with LeakSanitizer
disabled for the execution tracer. All 737 installed files retain their manifest
hashes. Windows owner testing of the new setup panel is pending.

Drag fixtures cover both board variants and both template encodings, coalesced
press/motion/release, extreme positions, window exit, focus loss/regain batches,
No Timer, Cancel and reopening. Existing fallback records remain unchanged.
External SDL software-backend runs verify a coalesced release, focus interruption,
applying 2:30 to a round and retaining it after menu return in both versions.
Retail captures and the preview harness remain outside Git.

The dragging refinement passes all 43 Boggle CTest checks with the supplied
install; the full suite passes 66 checks and skips 10 optional idtech3 data
checks. Frontend/options suites pass ASan/UBSan with LeakSanitizer disabled for
the execution tracer. All 737 installed files retain their manifest hashes.
Windows owner testing of dragging and interruption behavior is pending.
