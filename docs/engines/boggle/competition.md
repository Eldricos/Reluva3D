# Classic competition: scoring foundation

This stage establishes the final shared-word scoring model for the next
player/opponent work. It does not enable multiple players or computers in the
live game yet. The current playable session remains one human. Player turn
order, computer timing/difficulty and their UI must be established and integrated
separately; adding invented opponents would not establish original compatibility.

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
The current single-player results screen continues using its existing path;
future UI integration must use competition totals only after all player rounds
and dictionary decisions finish, then record each eligible human result once.

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
Play (1300), with optional computer cursor/background controls. Their operational
semantics, defaults and selection of resource variants remain to be corroborated.
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
snapshots. Existing single-player tests still apply without record updates.
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
