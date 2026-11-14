# Classic gameplay edge cases

The native controller now pauses gameplay while its window is inactive. Player
budgets, background computers, opening/closing transitions and cursor schedules
share one monotonic logical clock. Focus notifications are idempotent; backward
wall readings cannot refund time. An inactive-window overlay hides the board,
and input queued with focus regain is discarded. Music and already playing
sound samples continue; original audio inactivity behavior remains unverified.
A mouse-only Pause control is available during a round, including computer
turns. Resume is also mouse-only. Manual pause survives switching away and
returning; focus, manual pause and notices hold independent pause reasons.
The owner confirmed an original Pause option with no shortcut and automatic
pause on minimizing or switching away. Native button placement and appearance
are presentation choices; no keyboard shortcut has been added.

Dictionary review begins only after all players finish. Reviews run in roster
order; Yes All and No All apply to the current player's pending words. Approving
a word adds it to the shared native user dictionary and resolves matching
pending words for later players without another prompt. Earlier declined words
stay declined. Shared-word cancellation and high-score recording follow review.
Background budgets stop/complete at the last human's end using the existing
untimed/timed rules; waiting on review cannot add computer words.

In U.S. English keyboard input, Q adds the complete QU face; a following explicit
U is consumed once. Backspace removes a whole final QU cube. Mouse selection
already appends complete cube faces. Keyboard input now checks each cube as
it is entered and highlights a valid path. Impossible extensions leave the
previous valid prefix intact; later letters in the same text event are still
processed in order. Ambiguous prefixes can choose a different valid path as
the word grows, using a bounded native search. Exact original tie-breaking
between paths is not claimed. Bare Q cannot use a retail Qu cube as one letter.

The owner confirmed that too-short attempts keep the same Chess Timer turn
and show a minimum-length notice. A native modal notice now states the selected
variant's three/four-character minimum and waits for OK (or Enter/Space/Escape).
The notice freezes all gameplay clocks and consumes the acknowledgment batch.
Impossible board words cannot enter through mouse or keyboard, as reported
from the VM; they do not reach word submission or pass the turn.

New high-score ties precede older entries; a tie can displace rank ten. Zero
scores qualify if ranking permits, including completed replays and manually
ended rounds. Only humans enter the table. Scores are saved once per finalized
match; browsing, discovery and successful-save retries do not submit again.
Native version 2 persistence permits zero scores and reads existing version 1
positive-score tables without changing their stored tie order.

## Evidence

The supplied EXE has SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Read-only analysis and isolated execution remain outside every Git worktree;
the native implementation is independently written from these observations.

- The WM_ACTIVATEAPP handler at VA `0x4144b0` dispatches pause/resume through
  Classic's `0x429fa0` / `0x429fd0` wrappers. A synthetic-state harness calls
  that handler, the original deadline services and replacement clock/window
  services: 512 foreground/background observations match frozen inactivity and
  resumed countdowns. This establishes clock behavior, not original audio/UI.
- Minimum-length validation at `0x414d50` calls message service `0x414370`;
  the latter wraps its modal dialog with the same pause/resume virtual services.
  This independently supports freezing gameplay while the native notice is open.
- The high-score insertion service at `0x43e940`, using replacement profile and
  string services with invented names/scores, matches 616 top-ten insertion
  contracts spanning empty/full tables, ties, zeros and displacements. The
  Game Over caller at `0x41af90` excludes computers and has no positive-score
  prerequisite before insertion.
- Game Over `0x41af90` finishes players before iterating pending lists. It checks
  the shared user dictionary before prompting, resets all-choice flags per
  player, and resolves reviews before duplicate cancellation/high scores.
  This is static call/data-flow evidence, pending whole-dialog VM comparison.
- The play key handler `0x41e260` treats Q specially and suppresses the next U
  through the English Qu option. Its Pause/Break case does nothing. The
  keyboard rule is based on static analysis, pending the requested VM check.

Synthetic tests cover nested event batches, repeated focus notifications,
Traditional/Chess clocks, background computers and transitions, Qu input limits,
shared approvals/declines, delayed review, zero-score replay and v1 migration.
The same Chess player remains after a too-short attempt; duplicates pass.
Tests also cover ambiguous keyboard prefixes, rejection without cube reuse,
mixed mouse/keyboard entry and manual pause across focus changes for human,
visible-computer and background-computer turns. Exact original replay dialogs,
keyboard path tie-breaking and audio inactivity behavior remain open checks. This pass does not establish complete Classic fidelity.

The full normal CTest run passes 62 checks, including all 39 Boggle checks
with the supplied install; ten optional id Tech 3 data checks are skipped.
All 737 supplied installed files retain their recorded hashes. Only synthetic
fixtures and checksum records are stored in Git. Windows testing remains pending.
The synthetic Boggle suites also pass AddressSanitizer/UndefinedBehaviorSanitizer
with leak detection disabled for the execution tracer.

The subsequent [manual-pause presentation](pause-presentation.md) uses the installed
closed-lid still during an explicit pause. Resume restores the same board and
selection; inactivity and prototype views retain their full-screen cover.
Pause-specific animation timing, original dialog styling and audio remain
separate comparison work. Clock and input contracts above are unchanged.
