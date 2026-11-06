# Classic computer opponents

Original and Master support Beginner, Intermediate, Advanced and Mr. Boggle in
Traditional Play and Chess Timer, with optional [background play](background.md). On Players, open **Local Players / M**, choose
any of the four slots, and select a computer entry. Slots 2–4 may be empty.
Slot 1 may also be a computer; an all-computer match is supported. The roster is
session-only. Computer selection never changes the saved human name/history.
Human names that happen to match computer labels remain human participants.

Computers use `Dicts/AI.dic` from the owner's installation, intersected with
`Dicts/English.dic`. The bounded board solver finds one legal path per eligible
word, including Qu, without cube reuse. Approved personal words do not expand
computer vocabulary. Every computer submission goes through the normal round
validator, scoring and shared-word cancellation. Visible turns play word-length
sound cues; hidden background submissions are silent. Computers appear in
match results and word review, but only humans enter the high-score table.

The supplied install/CD has the needed dictionary. New imports include it when
present. Older imports remain playable with humans; selecting a computer without
`AI.dic` produces an instruction to re-import. There is no silent fallback to a
different vocabulary. No original executable code runs, and no game dictionary
is embedded in the engine, tests or repository.

## Native strength and pacing policy

The four labels and broad turn rules are corroborated by the owner. **The native
strength policy and cadence below do not reproduce the original AI algorithm.**
They provide playable opponents while the original strength/timing work remains
open. The implementation is independently written using the existing solver and
public round interface.

| Level | Eligible lengths above the mode's minimum | Deterministic vocabulary sample | Active time between words |
| --- | --- | --- | --- |
| Beginner | Up to minimum + 1 | One quarter | 6 seconds |
| Intermediate | Up to minimum + 2 | One half | 4 seconds |
| Advanced | Up to minimum + 4 | Three quarters | 2 seconds |
| Mr. Boggle | All eligible lengths | All | 1 second |

Sampling and ordering depend on the native board seed, slot and word, using an
independent integer hash. For a fixed board and slot, stronger levels include the
weaker levels' vocabulary. Different slots can choose different words/order.
Replay rebuilds the same plans and clears their clocks; New Board rebuilds them
for its new seed. A stalled event loop submits at most one word per frame and
schedules the next interval from that submission, without catch-up bursts.

Each computer takes a full timed Traditional turn, even if its plan is exhausted;
F1 / End Round can end it early. An untimed Traditional computer ends when its
plan is exhausted. In Chess Timer, it submits one word and passes the turn;
its interval uses cumulative active time, excluding other turns and handoffs.
An exhausted Chess computer retires rather than cycling empty turns forever.
These exhaustion policies are native and await VM confirmation.

A handoff to a computer is displayed once, then starts automatically. A handoff
to a human waits for Start Turn / Enter. Computer words enter the visible list
with their normal length-dependent writing cue. Human typing/cube clicks cannot
submit on a computer's behalf. F1 can retire the active computer, and Menu /
Launcher actions remain available. Computers do not move the pointer or expose
their future words. [Background play](background.md) now runs computers alongside
human turns with hidden words and visible running scores; the final summary
provides word-list review. Optional visible
computer cursor movement remains a future stage.

## Evidence and limits

Read-only observations of the supplied `Boggle.exe` (SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`)
and English language resources establish:

- The Players controls offer four opponent labels, string IDs 17001–17004.
  The owner corroborated these labels and four player slots.
- The player-creation path at `0x419445` constructs a common computer player;
  the following configuration path references `AI.dic` at `0x4194ab`.
  This establishes a computer vocabulary resource, not the native sampling rule.
- The computer configuration at `0x42cda0` changes level-dependent parameters;
  delay calculation at `0x42f070` considers word length and other state.
  Their exact Classic behavior and the native rates above have not been equated.
- The end-game score loop around `0x41b335` checks the player's computer predicate
  (vtable offset `0x68`) and skips the high-score writer for computer players.
- The English DLL's string 9002 describes all-computer play disabling background
  play. The native all-computer setup likewise uses visible turns.
- The owner confirmed Traditional whole-round turns, Chess word turns and
  cancellation of shared words for human/computer matches. Visible computer
  cadence, exhaustion behavior and detailed original vocabulary selection
  still need reference checks. A later owner check corroborates hidden background
  moves in both modes, final score/word-list reveal and computer high-score
  exclusion; see [background timing limits](background.md).

The installed `AI.dic` is 108,774 bytes, SHA-256
`f05dfb3b5f2771ac98cf75a838dc74303700beb67cc292ca065a5641e3466f1d`.
Analysis artifacts remain outside every Git worktree. No disassembly or original
algorithm was translated into the planner.

## Validation

`boggle.unit.computer` covers both boards and all four levels, legal Qu paths,
English intersection, nested vocabulary, deterministic reset, no catch-up burst,
search-budget failure clearing, invalid typed rosters, human-only older imports,
profile isolation, human/CPU name collisions and all-computer play. Scripted
Traditional and Chess matches exercise automatic handoffs, human-input blocking,
shared-word scoring, human-only high scores, timers, transitions and replay.

`reluva-tool boggle computer-check <folder>` checks 64 plans over two seeds,
both boards, four slots and four levels, submitting every planned word through
the round validator. It prints metadata only. For the supplied files:

```
Classic computer plans 64, legal submissions 2766
decoded computers: 3a0f0641f91550787fcf69d76ac4d0b6
```

The optional game-data suite repeats this check on the source and on an imported
copy in the external analysis directory. No retail words, boards or media are
exported into the build tree.
