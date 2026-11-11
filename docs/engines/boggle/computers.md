# Classic computer opponents

Original and Master support Beginner, Intermediate, Advanced and Mr. Boggle in
Traditional Play and Chess Timer, with optional [background play](background.md). On Players, open **Local Players / M**, choose
any of the four slots, and select a computer entry. Slots 2–4 may be empty.
Slot 1 may also be a computer; an all-computer match is supported. The roster is
session-only. Computer selection never changes the saved human name/history.
Human names that happen to match computer labels remain human participants.

## Verified vocabulary and search limits

Beginner, Intermediate and Advanced use `Dicts/AI.dic`, intersected with the
installed English dictionary. Mr. Boggle uses the complete `Dicts/English.dic`
board vocabulary instead. Approved personal words do not expand any computer's
vocabulary. The supplied AI dictionary has 18,649 unique entries, all present
in the 112,427-entry English dictionary.

All levels respect the mode's normal minimum: three letters in Original and
four in Master. The first three levels stop extending a path once its prefix
reaches nine letters. A final multi-letter cube can cross that boundary: eight
letters followed by Qu can yield a ten-letter word. The supplied `AI.dic` has
no ten-letter entries ending in Qu, so its actual common-opponent words stop at
nine letters. The former native minimum+1/+2/+4 caps have been removed.

Mr. Boggle's original search requests an upper bound of 100, which covers every
word the supported board/dictionary can form. The native reader/solver's existing
64-byte word bound covers the supplied English dictionary (maximum 12 letters)
and supported Classic dice. Mr. Boggle retains every discovered main-dictionary
word, including words missing from `AI.dic` and words longer than nine letters.
The owner also confirmed longest-word-first play and ending the visible turn
when this list is exhausted in both timed and untimed games. These rules are
now implemented. Equal-length ties retain board discovery order; measured movement
and hold parameters determine cadence.

Every candidate retains one legal path, including Qu, with no cube reuse. Every
submission still passes the normal round validator, scoring and shared-word
cancellation. Visible turns play word-length cues; background submissions are
silent. Computer results remain reviewable, and only humans enter high scores.

New imports still include optional `AI.dic`. An older English-only import can
now run humans and Mr. Boggle without that file. Selecting any of the first three
levels requires it and gives a re-import message if it is missing. A corrupt
auxiliary dictionary is not opened for a Mr. Boggle-only roster. Mixed rosters
containing a common opponent still require a valid auxiliary dictionary.

## Completed Classic AI behavior milestone

The common levels now use the measured difficulty gates, small-pool fallback,
suffix recognition and shared-ending/shared-prefix groups instead of fixed
quarter/half/three-quarter vocabulary samples and native hash order. Mr. Boggle
retains discovery order for equal-length words. Movement, cube holds, between-word
gaps and common-opponent retirement have measured schedules rather than fixed
one-second presentation intervals or waiting for the timer.

See [the full specification, evidence and presentation limits](ai-milestone.md).
The native random source, seed mapping and reconstructed geometry remain
independent; exact original seeded rounds and idle pointer wandering are not
claimed. Replay rebuilds the same native plans, and New Board uses a fresh seed.

Mr. Boggle ends on exhaustion. Common opponents complete their measured final
empty-search wait before retiring, unless their timer expires first. Initially
empty plans retire directly. Both Traditional and Chess use these rules, including
untimed turns. Background play uses the same word schedule on its continuous
clock. The previous native exhaustion policies have been removed.

A handoff to a computer is displayed once, then starts automatically. A handoff
to a human waits for Start Turn / Enter. Computer words enter the visible list
with their normal length-dependent writing cue. Human typing/cube clicks cannot
submit on a computer's behalf. F1 can retire the active computer, and Menu /
Launcher actions remain available. Computers keep their paths hidden unless
[Computer Cursor / V](cursor.md) enables visible tracing. [Background play](background.md) now runs computers alongside
human turns with hidden words and visible running scores; the final summary
provides word-list review. Optional visible
computer cursor movement now uses a separate [native presentation option](cursor.md).

## Evidence and limits

Read-only observations of the supplied `Boggle.exe` (SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`)
and English language resources establish:

- The Players controls offer four opponent labels, string IDs 17001–17004.
  The owner corroborated these labels and four player slots.
- Classic's constructor stores mode ID `0x2af9` at `0x428651`. The separate
  `0x2afa` search path must not be used as evidence for Classic difficulty caps.
- Beginner/Intermediate/Advanced are created through the common implementation
  at `0x42ca70`, then configured with levels 0/1/2 near `0x419647`. For the
  English language path, their setup references `AI.dic` at
  `0x4194ab`, `0x41955b` and `0x41960b` respectively.
- Mr. Boggle follows the distinct creation branch at `0x41967e`, using the
  implementation constructed at `0x401000`. Its shared create routine at
  `0x401240` binds the main dictionary at game offset `0x1fe4`; this branch
  never replaces that binding with the auxiliary dictionary.
- Common Classic search at `0x42d481` requests the mode minimum through nine;
  the base/Mr. Boggle search at `0x402fb0` requests the minimum through 100.
  Original recursive search at `0x443790` checks a completed word before deciding
  whether its prefix may be extended, accounting for a final Qu crossing.
- The common configuration at `0x42cda0` and delay routine at `0x42f070` establish
  measured base-word timing ranges, progression and candidate-count scarcity.
  An external harness executes these with original selection at `0x42e0e0`:
  2,304 isolated timing contracts match. The completed milestone also compares
  whole plans, nominal movement and retirement; see [AI evidence](ai-milestone.md).
- The end-game score loop around `0x41b335` checks the player's computer predicate
  (vtable offset `0x68`) and skips the high-score writer for computer players.
- The English DLL's string 9002 describes all-computer play disabling background
  play. The native all-computer setup likewise uses visible turns.
- The owner confirmed Traditional whole-round turns, Chess word turns and
  cancellation of shared words for human/computer matches. Nominal cadence and common-opponent retirement now have isolated reference
  comparisons; full VM visual/timing matching remains a presentation check.
  A further owner check confirms Mr. Boggle's longest-first and early-finish rules. A later owner check corroborates hidden background
  moves in both modes, final score/word-list reveal and computer high-score
  exclusion; see [background timing limits](background.md).

The installed `AI.dic` is 108,774 bytes, SHA-256
`f05dfb3b5f2771ac98cf75a838dc74303700beb67cc292ca065a5641e3466f1d`.
Analysis artifacts remain outside every Git worktree. No disassembly or original
algorithm was translated into the planner.

## Validation

`boggle.unit.computer` covers both boards and all four levels, legal Qu paths,
English intersection, difficulty thresholds and grouping, deterministic reset, no catch-up burst,
search-budget failure clearing, invalid typed rosters, human-only older imports,
profile isolation, human/CPU name collisions and all-computer play. Scripted
Traditional and Chess matches exercise automatic handoffs, human-input blocking,
shared-word scoring, human-only high scores, timers, transitions and replay.

`reluva-tool boggle computer-check <folder>` checks 64 plans over two seeds,
both boards, four slots and four levels, submitting every planned word through
the round validator, checking submission deadlines and hashing their times.
Common plans must remain in their dictionary/search bound; Mr. Boggle's pool
must equal the complete main-dictionary board vocabulary. It prints metadata
only. For the supplied files:

```
Classic computer plans 64, legal submissions 3817
decoded computers: e612fb5fc755e4bfc7665e01d913b71a
```

The optional game-data suite repeats this check on the source and on an imported
copy in the external analysis directory. No retail words, boards or media are
exported into the build tree.

An external reference harness runs the owner's EXE read-only in the project's
interpreter. With invented game/player/board state and replacement MFC/profile
services, it captures eight original search-entry contracts: two minimum lengths
across four opponents, with zero mismatches. Six further synthetic probes execute
the original recursive length guards, including the final-Qu crossing, with zero
mismatches. These are isolated binding/range/boundary observations, not a full
original AI round or word-order comparison. Separate isolated base-word timing
comparisons are documented in [thinking times](thinking.md). Harnesses,
original bytes and raw research remain outside every Git worktree.

Synthetic tests additionally cover common search boundaries and difficulty gates,
main-only long words for Mr. Boggle, auxiliary-free/corrupt-auxiliary Mr. Boggle
sessions, prefix-budget filtering, Mr. Boggle's longest-first ordering and timed/
untimed completion, plus existing visible/background/cursor flows.
Human-only fixture words used in shared-score tests are stored as invented
personal additions, so they remain outside computer vocabularies.
