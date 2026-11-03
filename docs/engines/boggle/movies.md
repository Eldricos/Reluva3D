# Intro and animated mode buttons

With **Intro and menu animations** enabled (the default), the native frontend
plays the installed `Anims/INTRO.AVI` once on entry. Escape skips to game selection;
video completion does the same. The entire input batch is consumed before showing
the menu so skipping cannot also select a game. Window close and Return to
Launcher requests remain available throughout playback. No gameplay timer runs
during the intro.

The intro uses its declared AVI cadence and PCM soundtrack. It is drawn at
600x360 at (20,60) in the 640x480 frontend, as established by the original intro
window setup at EXE VA `0x406b1e`..`0x406b2f`. Playback uses the native elapsed
clock and audio output; precise device latency/lip synchronization still needs
listening comparison. Missing audio hardware permits silent video. The movie
soundtrack is independent of the Classic effects and music selectors. Audio is
stopped before the menu appears or the engine exits, and decoded intro frames
are released before Classic loads its round clips.

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
audio. The supported intro is 300x180. Existing smaller imports remain usable.

The [shared Cinepak reader](../../common/avi-cinepak.md) documents provenance,
limits and independent reference comparisons. Source EXE SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
The original intro path is requested at `0x406ad9`. Header/packet inspection of
the owner's files establishes the formats and rates; no media is embedded in
code. The owner confirmed the original intro's Escape-to-main behavior in the VM.
Native controls and the remaining frontend limitations are in [frontend](frontend.md).

## Validation and import

`reluva-tool boggle movie-check <folder>` decodes available intro/button clips,
reporting only counts and a checksum. Both supplied expanded copies report six
movies, 548 video frames and 554,190 audio frames, checksum
`4d62f5db688fcb3b9921f16c450163b4`. The optional game-data movie step and external
import check commit only metadata. Raw decoded output stays outside Git.

New imports include all five available mode clips: 45 files / 13,892,691 bytes
for the supplied release, plan checksum `9fa2df147dd9cefe8039e1d912caa57c`.
`INTRO.AVI` was already retained by the original detection/import contract.
Older 39/40-file copies therefore gain intro playback, with static fallback for
missing button clips; re-import to include the five additional clips.

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
