# Intro and animated mode buttons

With **Intro and menu animations** enabled (the default), the native frontend
plays `Anims/Hasbro.avi`, a one-second gap, then `Anims/INTRO.AVI` once on
entry. Escape skips the remaining startup sequence to game selection, including
from the gap; completing the final clip does the same. The entire input batch
is consumed before showing the menu so skipping cannot also select a game. Window close and Return to
Launcher requests remain available throughout playback. No gameplay timer runs
during the intro.

Each clip uses its declared AVI cadence and PCM soundtrack. Hasbro is drawn
at 640x480 at (0,0); the game intro is drawn at 600x360 at (20,60) in the
640x480 frontend, as established by the original intro window setup at EXE VA `0x406b1e`..`0x406b2f`. Playback uses the native elapsed
clock and audio output; precise device latency/lip synchronization still needs
listening comparison. Missing audio hardware permits silent video. The movie
soundtrack is independent of the Classic effects and music selectors. Audio is
stopped between clips, before the menu appears, and when the engine exits.
Decoded intro frames are released before Classic loads its round clips.

Available 100x100 Cinepak mode clips (`ClsBut8`, `BrkBut8`, `BatBut8`, `SpcBut8`,
`TetBut8`, under `Anims/`, extension `.avi`) animate inside the existing native
buttons. Other modes remain disabled and labeled unavailable. Native playback
loops at each clip's declared rate while selection is displayed; original hover
triggers, menu sounds, typography and exact button presentation are not claimed
matched. Movie timing and the existing roll/lid timing are separate.

The launcher setting is `frontend_movies=1` or `0`. Disabling it skips the intro
and keeps static native menu buttons; it does not disable Classic roll/lid clips.
Direct Classic entry (`classic_menu=0`) bypasses frontend movies. Missing optional
clips use the previous static fallback. Present but malformed media fails with an
explicit reader error. Button clips must be 100x100, at most 120 frames, without
audio. The supported source dimensions are 320x240 for Hasbro and 300x180 for the game
intro. Existing smaller imports remain usable.

The [shared Cinepak reader](../../common/avi-cinepak.md) documents provenance,
limits and independent reference comparisons. Source EXE SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
The original Hasbro path is requested at `0x406bc9`, its full-window placement
is set at `0x406c0e`..`0x406c1f`, and the initial setup calls that loader at
`0x406aac`. Completion handling schedules the next stage at current tick +1000
(`0x406da6`..`0x406db1`); the scheduled callback invokes the game-intro loader
at `0x406ccd`. The game-intro path is requested at `0x406ad9`.
The Hasbro file is 1,271,606 bytes, SHA-256
`19b44a0444e8eda5e4fdb100efd36e2cb50baac7eaefaff1792e9ad922edf3be`.
The owner's Windows check of the initial movie pass confirmed game-intro and
button playback but exposed the missing publisher stage: it had been decoder-
validated but omitted from both playback and import. This correction adds it.
The native Escape policy skips all remaining startup stages; a Hasbro-specific
VM skip check has not been separately recorded. Header/packet inspection of
the owner's files establishes the formats and rates; no media is embedded in
code. The owner confirmed the original intro's Escape-to-main behavior in the VM.
Native controls and the remaining frontend limitations are in [frontend](frontend.md).

## Validation and import

`reluva-tool boggle movie-check <folder>` decodes available intro/button clips,
reporting only counts and a checksum. Both supplied expanded copies report seven
movies, 704 video frames and 783,510 audio frames, checksum
`abb22635617408befee5004becc849cc`. The optional game-data movie step and external
import check commit only metadata. Raw decoded output stays outside Git.

New imports include Hasbro plus the five mode clips: 46 files / 15,164,297 bytes
for the supplied release, plan checksum `65d8cd90bfb14962f0d8da74d3ebcfd0`.
`INTRO.AVI` was already retained by the original detection/import contract.
Older 39/40-file copies contain the game intro but lack the mode clips and
Hasbro. The preceding 45-file import contains the mode clips but lacks Hasbro.
Re-import to obtain the missing files. Absent optional clips are skipped so
previous copies remain playable.

Invented movie/PCM fixtures verify natural completion, Escape skip and repeat
guards, input at the completion boundary, animation looping, soundtrack cleanup,
window/launcher exit, missing-device fallback, disabled media and malformed files.
Their screen records contain only checksums of independently generated pixels.
The earlier no-movie frontend and direct-round records remain unchanged.

All 20 Boggle CTest checks pass with the supplied install. Movie/import records
also match the expanded CD copy. The ordinary full CTest run has 32 passing
checks and 20 optional data checks skipped without configured paths. The common
reader tests, Boggle tests and supplied movie decode pass ASan/UBSan (with
LeakSanitizer disabled for the execution tracer). A Boggle-disabled build also
passes the new shared reader tests and suite registration check.

An external SDL dummy video/audio run from a fresh 45-file CD import displayed
and skipped the intro, drew animated buttons, reached live Master and Original
boards, and released audio through menu returns and exit. Private captures were
viewed locally. This validates software integration, not physical output or
Windows compatibility. All 737 original install files still match their SHA-256
manifest. The owner needs to test the new movie/audio path on Windows.

Publisher-sequence regression checks use invented 320x240 and 300x180 movies to
verify order, placement, the native one-second gap, sound shutdown between
stages, sequence-wide skip and global exit in each stage. Disabled movies also
bypass a malformed publisher file. The optional import check now validates the
46-file set and decodes both soundtracks. Windows testing of this added
publisher stage remains pending.

The corrected publisher sequence passes all 20 Boggle checks and ASan/UBSan;
install/CD metadata agree. A fresh 46-file external CD import completed the
actual Hasbro movie and gap, started the game intro, then reached Master and
Original play through SDL dummy output. Four distinct audio sessions (publisher,
game intro and both Classic entries) were observed, with nonzero mixed output.
This remains software integration evidence rather than a Windows playback claim.

The owner subsequently confirmed that both intro movies now play on Windows.
