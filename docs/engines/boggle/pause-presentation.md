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
the settled paused view uses the closed-lid scene and native Resume panel;
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
