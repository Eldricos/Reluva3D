# Local Traditional Classic

Classic Original and Master now support one to four local humans in Traditional
Play. Each active player takes a full round on the same generated board. The
owner corroborated this broad turn rule in the original VM; computer opponents,
Chess Timer and network play are not implemented in this stage.

## Setup

On Players, use **Change Name / N** to create human names as before. Choose
**Local Players / M** to configure four human slots. Click a slot or press 1–4,
then select a remembered name with the existing dropdown controls. Slots 2–4
also offer an explicit empty-slot entry. Slot 1 always has a human. Done / Enter
or Escape closes this panel; a separate Play / Enter starts the game. To add a
name, close the panel, use Change Name, then return to Local Players.

Active slots play in slot order, omitting empty slots. A name may occupy more
than one slot; slots still have independent submissions and scores. This is a
native policy pending original duplicate-name setup checks. The roster lasts
for the current frontend session, including returning to the menu between
matches. Only the existing primary name/history persists across launches; no
new original player files or registry settings are read or written. The direct
Ready route with the menu disabled remains one human. No computer choices are
advertised as playable.

## Turns and results

The first player starts through the existing opening sequence. On timeout or
End Round / F1, the current player's round closes. Unknown words are resolved
using the existing dictionary review. A hidden-board handoff then identifies
the next player. Pass the controls and confirm **Start Turn / Enter**; the next
player's timer starts after their opening sequence, with the full configured
duration. Waiting at handoff consumes none of their round. Untimed turns end
with F1. Handoff hides the board, previous word list, all-word discovery and
final scores. Queued text is discarded; the start confirmation consumes the
rest of that input batch. Menu / F3 and global Quit/Launcher remain available.

After the last player's dictionary decisions, the match applies
[shared-word cancellation](competition.md) and shows all player scores. A word
kept by multiple players awards zero to every owner. Approved custom words
participate; declined words do not. Names never determine word ownership.

Press 1–4 or click a name on the match summary to review that active player's
words. Enter, Escape or V closes the summary. In review, 1–4 changes player and
V reopens the match summary. The existing word selection, numbered cube paths,
scrolling, All Words / A and High Scores / H remain available. Shared entries
are grey and show zero points; selecting one explains why it scores zero.
The final counted-word total excludes shared entries, while the review list
retains them. Missed-word discovery never changes anyone's score.

Replay / Enter resets every player's round on the same board and orientations,
starting again with the first player. New Board / F2 creates the next board for
all players and returns the first player to Ready. These actions are available
after the match finishes, not between unfinished player turns. Leaving through
the menu/launcher during a match does not publish incomplete match scores.

High scores record all eligible positive human totals in one atomic native
snapshot after the full match finishes. Failed validation/writes commit none
of the batch; an explicit retry saves the complete batch once. Browsing players,
reopening summaries or retrying after success does not submit scores again.
The existing high-score file format remains compatible.

## Native choices and evidence limits

The owner's VM observations establish full-round Traditional turns for humans
and computers, four player dropdowns and end-of-round shared-word cancellation.
[Competition research](competition.md) records the dialog/caller evidence and
16,384 isolated original score comparisons. This implementation uses independently
written round/controller logic and the existing native ownership model. It does
not execute or translate original routines at runtime.

The native handoff/summary layout and keys, session-only roster, empty-slot
compaction and same-name slots are explicit implementation choices. Dictionary
review currently occurs after each player's turn, using the existing shared
native user dictionary; an approved word can therefore be recognized during a
later turn. Original multi-player review timing and user-dictionary scope still
need corroboration. Exact turn-transition animation timing and original dialog
navigation also remain unverified. Chess Timer, AI strength/cadence, background
computer play and computer cursor movement remain separate future work.

## Validation

`boggle.unit.traditional` scripts both variants/renderers through two-player
approval/decline review, hidden handoff, discarded queued input, shared/missed
word review, final scores, Replay and New Board. It also covers three/four
humans, repeated names, per-player timeout after a long idle handoff, both lid
sequences, incomplete-match exits, live roster selection/removal and score-save
failure/retry. The competition/results tests check shared-row masks, preserved
paths, counted totals, clearing invalid snapshots and read-only discovery.
High-score tests check whole-batch validation, failed writes, restart and
repeated names. Existing single-player gameplay records remain unchanged;
menu records change only for the new setup text and player controls.

All 33 Boggle checks pass with the supplied install. The normal full suite
passes 42 checks and skips 24 optional data checks. Traditional, competition,
high-score and results suites pass ASan/UBSan with LeakSanitizer disabled for
the execution tracer. External SDL dummy runs completed two-human matches on
both installed variants and visually checked handoff, final totals and shared
word review. Captures and the harness remain outside Git. All 737 supplied
installed files retain their manifest hashes. Windows owner testing of this
stage is pending. No re-import or save-file migration is required.
