# Classic pause, focus audio and round-end presentation

The owner confirmed these original-game behaviors in the VM:

- Explicit Pause and Resume each play a lid animation; music continues.
- Losing window focus pauses the game and stops music.
- Round end has no lid animation.

Native Classic now follows those contracts in Original and Master. Pause closes
the lid before showing Resume. Resume reopens it before releasing the gameplay
clock. The existing board, selection, unfinished input and computer plans remain
intact; no fresh roll or shake sound is triggered. Results and dictionary review
appear directly at round end, keeping the open board.

## Animation and clocks

With Classic round animations enabled, Pause plays AVI frames 44..54 at 100 ms
per frame (1.1 seconds). Resume plays 31..43 (1.3 seconds), skipping the initial
covered-tray shake. The original close helper seeks frame 43 at `0x42a8a1` and
advances through 54; the raise helper seeks 30 at `0x42a0f6` and advances through
43. Initial round opening still plays 0..43 before starting the game timer.
Existing per-frame camera poses, cube clipping and mesh-style choices apply.
No new resources or imports are required.

A separate presentation clock advances lid motion while the gameplay clock is
held. Focus loss freezes both clocks, so switching away during either motion
cannot jump frames or consume playing time. Resume is available only after the
closed scene is displayed; game input remains blocked until reopening ends.
Repeated focus notifications and input queued with focus regain remain guarded.
Disabling animations uses the closed/open still views immediately, as before.

Inactive windows retain a full-screen cover. With installed artwork enabled,
the settled paused view leaves the closed-lid scene unobstructed and changes
the native Pause control to Resume in the same position;
player labels and counted words remain visible. Gameplay entry hints and action
buttons are hidden, while unfinished input is retained. The prototype retains
its full-screen settled pause cover. System-pointer restoration remains active
for controls and animation; no keyboard Pause shortcut is added.

## Music and evidence

Focus loss suspends the music channel without freeing it or advancing its sample
position. Focus gain resumes that channel, including while explicitly paused.
Manual pause does not mute music. Effects remain independent; the owner's report
establishes music behavior and does not establish a new effects policy.
The shared mixer's channel-pause operation holds its existing mutex, preserves
channel identity and position, and allows a paused channel to be stopped safely.

Executable identity is recorded in [die orientation](die-orientation.md).
Classic's wrappers at `0x429fa0` / `0x429fd0` distinguish explicit-pause lid calls
from focus-only pause. An external probe using invented state and intercepted
base/lid services passed 24 wrapper checks. The owner subsequently corroborated
both manual animations, music behavior and the absence of round-end motion.
These observations replace the previous native still-frame and end-closing
policies. Original files, raw research and probes remain outside Git.

## Pause control and settled view

The Classic pause command at `0x429cc0` toggles its explicit-pause flag and calls
the pause/resume virtual service with argument 1. It changes menu command 4025
between string IDs 4009 and 4010; this path does not construct a modal dialog.
An external isolated probe using invented objects and replacement string/menu
services matches 20 cases across both initial pause states, repeated toggles and
five controller states. Original code, resource text and the probe stay outside
Git. This establishes the toggle contract, not exact whole-screen appearance.

The installed-artwork view therefore no longer puts a native floating panel on
top of the settled lid. Its existing bottom Pause control becomes Resume after
the closed scene has been displayed. This is a native adaptation of the original
menu toggle, not an implementation of the original Windows menu. The prototype
keeps its full-screen pause view and previous Resume target. No keyboard shortcut
is added. Music, lid frame ranges, clock ownership, focus cover and round-end flow
are unchanged; no additional imports or save migration are needed.

A second click queued with Pause, a click during closing, and a click queued with
focus regain cannot resume the game. Resume consumes its input batch, preserving
unfinished input for a later submission. Clicking the retired floating-panel
location also leaves the game paused. Explicitly leaving for the native launcher
remains available under the existing navigation rules.

## Validation and limits

Synthetic cases verify frame boundaries, deadlines, repeated resume attempts,
focus suspension during lid motion, all style/variant combinations, preserved
input submission and immediate open-board results. PCM comparisons verify music
continuity, silence during focus loss, independent effects and safe channel stop.
Existing Traditional/Chess clock, background-computer and cursor checks remain
in use. The presentation record now captures immediate results and a later
replay instead of a round-end closing clip. Default resource digests are unchanged.

Full regression, focused sanitizer and external SDL checks validate integration.
Exact original dialog styling and hardware-specific audio/raster timing remain
comparison work; this does not claim full Classic visual parity.

For the pause-control refinement, synthetic installed-artwork rounds compare the
entire area formerly hidden by the floating panel against the closed-lid image.
Original/Master, both mesh styles and the meshless fallback retain their board
and input on resume. Traditional/Chess, visible/background computer and prototype
checks retain clock/focus guards. External SDL software-backend checks inspect
settled pause, lid reopening and immediate results in both variants/styles.

All 43 Boggle CTest checks pass with the supplied install. The full suite passes
66 checks and skips 10 optional idtech3 data checks. Cube-renderer, Chess, cursor
and presentation suites pass ASan/UBSan with LeakSanitizer disabled for the
execution tracer. All 737 installed files retain their manifest hashes. Windows
owner testing of the refined pause view is pending.
