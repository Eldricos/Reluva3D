# Classic background computer play

On Players, configure at least one human and one computer through **Local
Players / M**, then click **Computer Background Play** or press **B**. Traditional
and Chess Timer remain separate choices under **C**. Background play defaults to
off and lasts for the current frontend session. The private target setting
`classic_background=0` or `1` supports controlled runs; other values are rejected.
No additional launcher option or import dependency is introduced.

With background play enabled, humans keep control of the board. Computers have
no visible turn or handoff, move no pointer and play no word-entry/timer sounds.
Their word lists stay hidden. A bottom strip shows every player's running score,
including computers, so computer totals can rise while a human is playing or
switching turns. These are provisional totals; final shared-word deductions
apply at match end. Traditional still gives each human a full
turn; Chess rotates recorded/repeated words among the remaining humans. With
one human, Chess word entry continues without a handoff to the hidden computer.

After all human turns and dictionary decisions finish, the existing match
summary shows final scores, including computers. Choose a player row or press
1–4 to inspect that player's words, paths and shared-word deductions. All Words
and replay remain available. Computer words participate in the same shared-word
cancellation as human words. Only humans enter high scores, and choosing a
computer never changes the remembered human-name history.

An all-computer roster uses visible computer turns, even when a background
setting was requested. The Players switch is unavailable until a human and a
computer are assigned. A human-only roster also ignores the background request.
The roster's explicit participant kinds decide this, independently of names.

## Timing policy and verification limits

The owner observed the following in the original VM:

- Background computers make no visible moves in both Classic turn modes. Their
  words are listed after the human's time ends; final scores are shown first.
- All players' current scores appear along the bottom during play, and computer
  scores can be seen increasing.
- Each computer receives the same time budget as a human, with a continuous
  clock that does not pause for player switches.
- When all humans end a timed game early, computers immediately finish with the
  scores they would have obtained using their full budget.
- When all humans end a Not Timed game, computers stop when the last human does.
- Computer scores do not enter View Highscores.

The native implementation applies these broad presentation/timing rules and
retains the independently written [computer vocabulary/strength policy](computers.md).
A computer's scheduled interval is 6/4/2/1 seconds for
Beginner/Intermediate/Advanced/Mr. Boggle. Hidden play processes all scheduled
words that have become due, in logical time order; it does not depend on frame
rate or the number of input events. Every word passes the normal round validator
at its scheduled time. A word scheduled exactly at a timed deadline is excluded.

Each background computer gets one selected round duration for the whole match;
human turns do not reset or pause that budget. The native clock begins when the
first human's board opens. Subsequent switches and lid sequences keep it running.
It also continues through intermediate human dictionary review; this particular
review-timing detail and the exact original initial-start boundary remain
unverified. In untimed play, the clock freezes at the last human's end before
that player's dictionary review, so review waits cannot add more computer words.
In timed play, an early final human end processes the unused part of the selected
budget immediately, without a visible extra turn or a real-time wait.

If a computer exhausts its vocabulary early, its running score stays at the total
already found. Final scoring still cancels shared words. Native difficulty,
word ordering, score-strip layout and precise original AI cadence remain separate
comparison work; these observations do not establish original AI equivalence.

Replay resets all rounds, clocks and plan cursors, retaining the board/seed and
word schedules. New Board rebuilds them for the new native seed. Leaving for the
menu does not publish a partial match or save its unfinished scores.

The original English DLL describes all-computer play disabling background play
in string 9002, and the owner previously corroborated separate background and
cursor options. That is the evidence for the all-computer fallback and separate
native switch. The separate [visible computer cursor](cursor.md) option is now
available and stays inactive during background play.
No original algorithm, disassembly or retail data was copied into this stage.

The common opponents now use variable [base-word thinking delays](thinking.md).
Selected [plural](plurals.md) and bounded [past-tense](past-tense.md) follow-ups
share one thinking phase per root, with native presentation intervals. Sampling,
group construction and other related words remain native.

## Validation

`boggle.unit.background` exercises both boards, both turn modes and both renderers.
It checks that hidden words change neither the human board/word-panel framebuffer
nor audio, while the separate live score strip rises and final shared-word
cancellation/computer review work. Other cases
cover frame-independent schedules, exact deadlines with 60+ synthetic words,
initial-lid exclusion, CPU slots before/between humans, continuous progress through
human handoffs, timed early completion versus untimed stopping before final
word review, replay/New Board, the Players keyboard/mouse switch, strict option
parsing and all-computer fallback. Fixtures contain invented data only.

`reluva-tool boggle background-check <folder>` validates 256 schedules over two
seeds, both boards, four slots and four levels, with 60/180/300-second and
untimed budgets. It compares coarse and fine service intervals and submits every
word through the canonical round model. For both supplied install/CD copies:

```
Background schedules 256, legal submissions 11976
decoded background: c996760e42d988caca13fe570199a069
```

The optional game-data suite runs this check on the source and a fresh imported
copy. Only counts and checksums are recorded; raw research output and game files
remain outside every Git worktree. This checks native scheduling consistency,
not agreement with the original AI algorithm.
