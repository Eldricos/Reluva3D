# Visible computer cursor movement

The [completed AI milestone](ai-milestone.md) replaces the former final-second
preview with each plan's measured travel/press/release timeline. Timing still
applies with the cursor hidden. The installed raised/pressed hand now replaces the native arrow when its
resources are available; interpolation remains native. See the
[presentation fidelity pass](presentation-fidelity.md).

On Players, assign a computer through **Local Players / M**, then click
**Computer Cursor** or press **V**. This is independent of Traditional / Chess
Timer. It defaults to off and lasts for the frontend session. The private target
setting `classic_cursor` accepts only `0` or `1`; it adds no launcher checkbox.

With the option enabled, an animated pointer visits a visible computer's planned
cubes, highlights the selected prefix and builds the input one cube at a time.
Qu contributes two letters in a single cube step. The word enters the list and
scores at its existing scheduled submission time, with the existing word-length
sound cue. Computer strength, word ordering and submission cadence are unchanged.
Partial traces disappear if time expires or the player ends before submission.

The pointer is drawn inside the game framebuffer and follows the current board
geometry, including the installed perspective artwork or the prototype board.
It uses the installed raised/pressed hand images, with a native arrow fallback. The system pointer is hidden during visible
computer turns and restored for human turns, handoffs, results and leaving the
game. Its desktop position is never moved. F1 / End Player and Menu / F3 remain
available during tracing. Human input cannot submit words for a computer.

Background play keeps this option inactive: hidden words remain hidden and the
user pointer stays available. An all-computer roster still uses visible turns,
so its cursor option remains available even if background play was requested.
Disabling background play restores the session's saved cursor preference.

Replay resets the trace and word plan for the same board. New Board starts a new
plan. Clicking the word list during human play also submits the current selection or
typed input, using the same validation, repeat and dictionary-review rules as
Enter. History arrows retain their scrolling action, and clicking a completed
result still reviews that word's path.

No separate cursor bitmap or other new import file is needed; existing computer-capable
imports work unchanged.

## Reference evidence and native presentation policy

The owner previously corroborated an optional computer cursor control, separate
from background play and from Traditional / Chess Timer. In the supplied
`Boggle.exe` (SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`),
Classic setup at `0x41dceb` references the `MoveCursor` setting. The following
profile read stores its result at `0x488030`. The options code also reads that
setting at `0x44f19d` and writes it back around `0x44f1ef`. These read-only
observations establish a configurable option, not the native interpolation rule.
No disassembly or original cursor data was translated into this implementation.

The owner subsequently confirmed that the cursor visits cubes one at a time in
word order, then moves to the word list to submit. This is the same in Traditional
and Chess Timer. Clicking the word list also submits human selections in the
original. The implementation now follows these broad interaction rules.

The live animation follows each plan's measured cube travel, press, hold, release
and word-list submission timeline. Press boundaries advance the displayed input
by one cube, including a complete Qu face. Travel uses the native scene's cube
centers; the arrow moves between the scheduled boundaries without consuming or
rescheduling a word. Late frames can skip intermediate positions without causing
a catch-up burst of visible submissions.

The installed cursor/state mapping is now established; between-event
interpolation and cube shapes remain native. [Playing-camera projection](camera.md)
now provides verified camera/grid geometry for the cursor destinations.
Exact camera/idle pointer motion and frame-for-frame VM matching remain part of
presentation comparison. Selection, nominal timing and retirement evidence are
in the [completed AI milestone](ai-milestone.md).

## Validation

`boggle.unit.cursor` checks cube-to-list interpolation, Qu prefixes,
invalid geometry, drawing clips and strict settings. Scripted tests cover both
boards, both renderers and both turn modes, comparing cursor-on and cursor-off
final results. They also cover input isolation, Chess handoffs, manual ending,
expiry, quit/menu exits, Replay/New Board, background suppression and keyboard/
mouse setup controls. Word-list clicks are compared with Enter for selection,
repeats, invalid words and dictionary review. Pointer visibility must be restored after every exit.
All fixtures are invented and contain no retail cursor, board or word data.

The existing metadata-only `computer-check` now also previews every planned
path and verifies that previewing cannot consume/reschedule it. After the
[AI vocabulary correction](computers.md), its supplied-file survey covers
64 plans and 3,817 canonical submissions, including timing/motion metadata.
The optional game-data suite repeats that validation on the install and a fresh
external imported copy. No retail images or decoded word lists are exported.
