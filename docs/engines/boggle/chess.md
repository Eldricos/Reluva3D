# Local Classic Chess Timer

Original and Master now support Chess Timer for one to four local humans or computers.
On Players, click the mode button or press **C** to switch between Traditional
and Chess Timer, then use Local Players / M to configure human or computer players.
A separate Play / Enter starts the match. Traditional remains the native
default. The mode/roster lasts for the frontend session; the private target
setting `classic_turns=chess` or `traditional` also selects it for controlled
runs. Invalid values fail before graphics start. [Computer opponents](computers.md)
use an explicit native strength/pacing policy; background play and computer
cursor movement remain unimplemented.

## Word turns and clocks

Each participant has a separate time budget. Only the active player's time advances;
other players' time, the initial opening and hidden handoff waits do not consume
that budget. Returning to a player resumes their remaining time and preserves
their submitted words. The existing 1–5 minute settings in 30-second steps and
Not Timed remain available. With one human, words return to that same player
without a handoff.

A new dictionary word or a traceable unknown word passes play to the next active
player. A repeated word also passes the turn, adding no second entry or points.
Short/invalid/no-path attempts stay with the current player and use the existing
feedback. In particular, a path-search budget requiring explicit cube selection
cannot pass an unrecorded word. Unknown words stay red with provisional points
and are resolved when that player finishes, using the existing review controls.

After a word passes, a hidden-board panel identifies the next player and their
paused remaining time. Pass the controls and press Enter / Start Turn. Queued
text and extra input from the submitting player cannot enter the next player's
word field. Dice/lid opening plays once at match start; per-word switches do not
roll or reopen the board. The closing sequence plays after the last player ends.

When a player's budget expires, unfinished input is discarded and other players
continue. Rotation omits finished participants and identifies them in a notice
alongside the next active player. If only one player remains, reaching a finished
slot still shows that notice and returns play to the remaining player. Native
**End Player / F1** ends the active player's participation early; it does not
end the other players. End each active player with F1 in an untimed match.

Countdown ticks and expiry ringing track each player's budget independently.
Resuming the same budget second does not replay an already emitted tick.
Once everyone finishes and resolves their dictionary choices, the existing match
summary cancels shared words, offers per-player review/All Words, and records
eligible human high scores as one atomic batch. Replay resets all word lists
and budgets on the same board; New Board also changes the board. Menu / F3 and
Quit/Launcher can leave an incomplete match without publishing its scores.

## Evidence and native choices

The owner confirmed in the original VM:

- Chess Timer switches players after each word; each has their own clock, which
  stops while another player takes a turn.
- Traceable words absent from the dictionary enter the list for later review.
  Short words produce a notice; repeated words pass the turn.
- One player's time expiring does not end the whole game. A subsequent turn
  for that finished player produces a game-over/next-player notice.

The supplied executable SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Read-only inspection identified player timer helpers at VAs `0x44aa30`
(pause marker), `0x44aa50` (resume/deadline adjustment) and `0x44ab10`
(remaining time). An external x86 harness invokes these original helpers on
synthetic player state with a controlled GetTickCount service. It compares
4,608 positive-budget pause/resume observations over nine configured durations
and 128 switches per duration against the independently written native clocks:
zero mismatches. These checks cover isolated timer behavior within live budgets;
original raw overdue return values and the whole GUI/game loop are not claimed
to match through that harness. Captures, the harness, proprietary images and raw
research output stay outside Git. Engine code executes none of those routines.

Native clocks aggregate each player's active elapsed time, rather than copying
the original absolute-deadline representation. They clamp backward wall-clock
readings, validate player indices and avoid unsigned elapsed-time wrap. Each
round receives its own cumulative active time while animations use wall time.
This preserves dictionary/duplicate state across turns without restarting a
round on each word. The controller also settles expiry reached while servicing
a submission before handing off, so pending dictionary entries cannot be
stranded in a finished player.

The mode button/keys, hidden handoff/confirmation, consolidated notices for
several finished slots, initial-only opening/final-only closing and F1 retirement
are native controls/presentation choices. Original exact notice timing, animation
sequence, short/no-path turn boundaries and multiplayer dictionary-review timing
still need detailed comparison. In particular, this pass retains per-player-end
review rather than claiming the original's exact multi-player prompt schedule.
Active time continues through lost focus as in the existing native game; original
focus/pause behavior remains unverified. The separate original computer options
must not be confused with this human turn mode.

## Validation

`boggle.unit.chess` checks mode parsing, per-player active clocks, frozen waits,
backward readings, index/count bounds and large elapsed values. Scripted rounds
cover new/repeated/unknown word passing, short/no-path feedback, queued-input
guards, deferred decisions, shared final scores, review/discovery, Replay and
New Board in both variants/renderers. Additional cases cover independent exact
deadlines after long handoffs, the initial/final lid sequences, one-ms resume,
solo play, mode/roster selection, three/four humans and finished-player rotation.
The submission-boundary regression fails when its controller fix is removed.
Synthetic audio checks verify separate per-player tick/ring deduplication.

All 34 Boggle checks pass with the supplied install. The normal full suite
passes 43 checks and skips 24 optional data checks. Chess (six cases), Traditional
and presentation suites pass ASan/UBSan with LeakSanitizer disabled for the
execution tracer. Existing Traditional gameplay records remain unchanged; menu
records reflect the mode button. External SDL dummy runs completed two-human
Chess matches on installed Original/Master resources, checked repeat-to-pass
and finished-slot notices, and visually verified final totals/shared review.
Captures remain outside Git. All 737 supplied installed files retain their
manifest hashes. Owner Windows testing of this stage is pending. No re-import
or save-format migration is required.
