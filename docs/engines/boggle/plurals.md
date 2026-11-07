# Common opponents' plural follow-ups

Beginner, Intermediate and Advanced can now play a selected base word followed
by one selected plural formed by appending `S` or `ES`. Both entries must already
belong to that opponent's native plan, with their own legal paths. This does not
expand its vocabulary or change the deterministic difficulty sample. Mr. Boggle
keeps its separate longest-first order and one-second native cadence.

## Verified behavior

Isolated execution of the supplied original Classic routines establishes:

- The common plural classifier recognizes an existing candidate equal to the
  base plus `S` or `ES`, subject to a difficulty-dependent random gate. It chooses
  the first eligible plural from its candidate list.
- Selection returns the enabled plural without consuming it when previewed.
  Consuming it clears that choice and advances past the group when it is empty.
- The plural's thinking phase is zero milliseconds and does not advance the
  base-word thinking bounds. This is not a claim of instantaneous visible word
  submission: cursor movement and other presentation steps remain separate.

The original contains additional suffix and shared-prefix cases. Those are
outside this implementation. No general English inflection rules are inferred
from the two verified append forms.

## Independent native pairing policy

The native planner first selects exactly the same bounded dictionary/path pool
as before. It then forms disjoint pairs, considering shorter bases first and
using the existing native hash order for equal lengths. If both append forms
are present, it prefers `S`. A word can belong to only one pair; pairs do not
chain recursively. The pair is placed at its base's former native rank, followed
immediately by its plural. Unpaired words retain their relative native order.

These pairing choices are **native policies**, not a reconstruction of the
original classifier's candidate order, probability gates or complete AI strength.
Every already-selected eligible pair is used; no additional random gate is
introduced. Replay deterministically restores the same pairs and timing.

Each base/unpaired word receives the existing [base-word thinking bounds](thinking.md),
using the number of remaining native groups and the base-group ordinal. A plural
does not incur another thinking phase or scarcity penalty. It receives a native
one-second presentation interval, with or without Computer Cursor enabled,
allowing the existing cursor preview and ordinary submission flow to complete.
The exact original travel/submission interval remains unverified. Visible play
still emits at most one word per frame; background play uses the same logical
schedule with silent words and visible running scores.

## Evidence and validation

The reference is the owner's read-only `Boggle.exe`, SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
An external interpreter harness executes the original plural classifier at
`0x42e6a0`, selection at `0x42e0e0`, configuration at `0x42cda0` and timing at
`0x42f070`, with invented strings/lists and replacement MFC, clock, random and
display services. Ninety-six isolated contracts cover all three levels, both
Classic minimum lengths, visible/background flags, both append forms and four
invented bases, including a Qu-like spelling. They verify the selected word,
non-consuming preview, consuming advancement and zero additional thinking.
Two further probes verify first-eligible-plural selection when input order is
reversed. These are not complete original rounds or original board-path checks.

The implementation uses an independently written map-based pairing pass and the
existing canonical round validator. Synthetic tests cover pair eligibility,
sample preservation, ambiguous/overlapping pairs, missing dictionary entries,
both boards, Qu paths, timing, replay and frame-independent background servicing.
The supplied-data checks continue to validate all planned words through the round
model and hash their scheduled times, on the install, CD and external import.
Original binaries, raw observations and the research harness remain outside every
Git worktree; no original implementation is copied or mechanically translated.
