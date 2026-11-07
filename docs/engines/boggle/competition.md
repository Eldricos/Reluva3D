# Classic competition: scoring foundation

The final shared-word model now backs [live Traditional local-human matches](traditional.md)
with one to four humans. The owner corroborated the broad turn-mode distinctions
below. [Chess Timer](chess.md) now also uses this model for local humans.
[Computer opponents](computers.md), [background play](background.md) and
[cursor movement](cursor.md) now use the same ownership model. Original
selection/pacing behavior still needs further boundary checks.

`ClassicCompetition` accepts one to four distinct, completed `ClassicRound`
objects on exactly the same Original or Master board. Every player's dictionary
decisions must be finished. It snapshots the kept entries and marks the players
who kept each word. A word kept by two or more players awards zero points to
every owner. A word kept by exactly one player retains its normal length score.
Declined words never participate. An approved custom word participates normally,
even when another player declined that word. Repeated submissions within one
player's round remain blocked by the existing round model.

Each result row retains its original accepted path, dictionary state and base
points, plus an owner mask and final awarded points. Qu remains one path cube
and two letters for scoring. Each player's score is the sum of awarded points.
The snapshot owns its data and survives replaying or changing the source rounds.
Input order identifies player slots; names and their persistence belong to the
future player setup, not to word ownership. The same name in two slots must not
merge their submissions.

Invalid counts, null/repeated round pointers, mismatching boards, unfinished
rounds or pending review fail with no partial published competition. Source
round scores/words, user dictionaries and high-score files are not changed.
Single-player results retain their existing path. Live Traditional matches
publish competition totals only after all player rounds and dictionary decisions
finish, then save all eligible human scores as one native batch.

## Original evidence

Read-only inspection used the supplied binaries, identified by full SHA-256:

- `Boggle.exe`: `5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
- `Language/English.dll`: `9922ca1656fa8a9f376b79cdc3c1a4a71bd0d75f01156e07e4c3a126304628fe`.

The extended player dialogs 10000 and 10830 occur at DLL file offsets `0x2940`
and `0x2be4`. Both have four name/opponent combo controls 4107–4110, with four
portrait controls 4111–4114. Their prompt directs users to type names or choose
computer opponents from the dropdowns. They establish four available slots,
not the exact runtime turn order or defaults. These are extended dialogs;
the earlier generic dialog dump misparsed them, so the corrected DIALOGEX
reader was used for these facts.

The player-setup initialization near executable VA `0x44b815` loads resource
string 17000, whose value is the count four, then near `0x44b8f0` loads strings
17001–17004 for the opponent combo entries. The English labels are Beginner,
Intermediate, Advanced and Mr. Boggle. Strings 14000–14004 have different labels
and must not be substituted for this dropdown without checking their callers.

Classic options resources 10823/10834 contain Chess Timer (1299) and Traditional
Play (1300), with optional computer cursor/background controls. The
broad turn-mode behavior now has owner VM corroboration below. Defaults,
precise timer accounting and selection of resource variants remain unverified.
Dialog 15900 at offset `0x5374` has a next-player message and OK, which establishes
a player transition prompt but does not alone establish when it appears.

The end-of-round code near `0x41b2ba` iterates four player slots, compares each
existing player's words with every other player through `0x44ade0`, and recomputes
scores through `0x44af00`. Matching words are marked and excluded from the final
sum; the normal length scorer is at `0x41a040`.

An external x86 reference harness loads the owner's executable read-only and
invokes the original comparison/recompute routines with invented word lists.
It substitutes only CString storage, allocation, destruction and string
comparison services; the original comparison, flags and scoring execute in the
existing interpreter. Every pair of subsets of six invented strings of lengths
3–8 was checked in both directions at minimum lengths three and four:
16,384 score comparisons, zero mismatches against unique-owner totals. This is
isolated scoring evidence, not an original full multiplayer round or AI test.
The harness, binary images and raw research outputs remain outside Git. The
native model uses an independently written ownership map; no disassembly or
original routine was translated into engine code.

## Owner VM corroboration

The owner subsequently confirmed the following in the original Classic game:

- Four dropdown menus offer previously used human player names and the four
  computer choices Beginner, Intermediate, Advanced and Mr. Boggle, in that order.
- At round end, words found by both players lose their points in both
  human-versus-human and human-versus-computer games.
- Traditional Play gives each player a whole-round turn before advancing to
  the next player, for both humans and computers.
- Chess Timer advances to the next player after each word, for both humans
  and computers.
- An option allows computers to play at the same time as other players.
  Another option controls whether a computer moves the cursor during its turn.

These observations corroborate the player controls, opponent labels and shared-word
scoring evidence above. They also establish a human-name history in the original
selector; retention across process restarts, its file format and duplicate-name
selection rules have not been established. The native profile now retains a bounded human-name history for selecting its
primary human and local-human slots; see [name selection and native limits](players.md).
It does not import the original player database. The concurrent-computer and
cursor options are separate from the Traditional/Chess Timer turn mode; they
must not be collapsed into one opponent setting.

A later owner VM check confirms separate paused Chess clocks, deferred checking
of unknown words, a short-word notice, repeat-to-pass behavior, and continued
play with a finished-player notice after someone runs out of time. The native
[Chess controller and clock checks](chess.md) now implement those broad rules.
Exact animation/dialog timing, multiplayer review scheduling, short/no-path turn
boundaries and computer strength/cadence remain unverified.

## Tools and validation

`reluva-tool boggle competition-check <game folder>` discovers playable words
on both explicit-seed reference boards and constructs one-, two- and four-player
final results with controlled overlap. It verifies scores against separate
ownership totals and checks retained paths, emitting only counts/checksums.
It neither exports word lists nor runs proprietary code. It works on the
existing supported import set without adding `AI.dic` or other resources.
This diagnostic checks the native model; it does not simulate an AI opponent.

`boggle.unit.competition` uses invented dictionaries and boards to check
one/two/three/four players, shared and unique words, Qu scoring/paths, approved
versus declined custom words, finalization guards, board identity and independent
snapshots. Existing single-player gameplay records remain unchanged; menu
records now include the live local-player controls.
The optional `boggle.gamedata.competition` check and import validation also run
the diagnostic on supplied and freshly imported resources. Its records contain
only metadata/checksums, with raw output outside Git.

All 32 Boggle checks pass with the supplied install. The normal full suite
passes 41 checks and skips 24 optional data checks. Competition/results unit
checks and the installed-resource competition diagnostic pass ASan/UBSan with
LeakSanitizer disabled for the execution tracer. The diagnostic checks six
competitions and fourteen player results, checksum
`be5fae83b0735277bba82da2d5d62d3f`. All 737 supplied installed files retain their
manifest hashes. No new assets, re-import or save migration are needed.

The [computer-opponent stage](computers.md) now connects four typed computer
levels to the same Traditional/Chess controllers and final ownership scoring.
Only human final scores enter the native high-score batch. Original AI
strength/cadence remains unverified; the playable policy is explicitly native.

The owner later confirmed background computers make no visible moves in both
Classic turn modes; their final scores and words appear after the human's time
ends, and computer scores do not enter View Highscores. Native
[background play](background.md) now applies these presentation/scoring rules,
with a continuous computer budget across player switches. A further VM check
confirms visible running totals, full-budget completion when all humans end a
timed game early, and last-human stopping for untimed games. Exact AI cadence
and intermediate dictionary-review timing remain unverified.
