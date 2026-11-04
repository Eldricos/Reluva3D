# Game selection and Classic setup

The engine now plays the available intro, then opens game selection before
Classic. Escape skips the intro; see [movie playback](movies.md). When present,
`Bitmaps/Select1.bmp` supplies the installed game's 640x480 background. The
controls are native: choose Classic, then Original or Master, the round length,
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
- Players: shows the supported one-human-player name, chosen version and
  duration. Change Name / N opens the native name editor; save or cancel returns
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
write the launcher configuration or original Windows registry. The supported
session remains one human player in U.S. English. Sound effects and music still
use the launcher's settings. A random music choice is made once per engine
launch, including across menu re-entry. Returning to menus stops the round's
music; entering Classic again restarts that same selected loop. Replay within
Classic continues its existing music cursor. These are explicit native policies;
exact original menu audio still needs VM comparison. Launcher/L and Menu/F3
are native convenience controls, not claimed original shortcuts.

**Game selection menu**, enabled by default in launcher options, is saved as
`classic_menu=1`. Disable it (`0`) to use the previous direct Classic Ready screen
and Escape-to-launcher behavior. This also allows direct regression comparisons.

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
