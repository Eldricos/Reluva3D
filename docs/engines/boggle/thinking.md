# Classic computer thinking times

**Milestone update:** The bounded native policy described below is retained as
historical evidence. The [completed AI milestone](ai-milestone.md) supersedes
its grouping, fixed presentation intervals and exhaustion-policy limitations.

The common opponents now use variable base-word thinking delays. This replaces
the native fixed six/four/two-second intervals. Mr. Boggle keeps its native
one-second interval while its separate movement/submission sequence is studied.

## Behavioral specification

Isolated execution of the supplied original EXE's Classic common-opponent
selection and timing routines establishes the following base-word delay bounds.
Times are milliseconds; the random upper bound is exclusive.

| Opponent | Initial minimum | Initial random span | Minimum growth per base word | Random-span growth per base word | Scarcity threshold | Extra delay per missing candidate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Beginner | 1500 | 2000 | 150 | 700 | 9 | 3000 |
| Intermediate | 1200 | 1700 | 50 | 500 | 7 | 2000 |
| Advanced | 1000 | 1000 | 50 | 300 | 5 | 1000 |

A base-word attempt starts at the current minimum, adds a random offset within
the current span, and adds the scarcity penalty when fewer candidates remain
than the threshold. Subsequent base-word attempts increase the minimum and span
by the listed amounts. The remaining count includes the current candidate. It
counts pending candidate words, not letters or cubes, so Qu is not a special
case for this part of thinking time.

For example, a first Beginner base word with at least nine pending candidates
has a 1500–3499 ms thinking delay. With only one pending candidate, that becomes
25500–27499 ms. Advanced's corresponding ranges are 1000–1999 and 5000–5999 ms.
These describe isolated thinking phases, not complete wall-clock time between
visible submissions.

## Native integration and limits

The native planner independently constructs its existing sampled word list.
Each base/unpaired word receives the bounds above, using the native list's
remaining group count and base-group ordinal. [Plural follow-ups](plurals.md)
and the supported [past-tense forms](past-tense.md)
now avoid a second thinking phase and use a native presentation interval.
A separate integer hash supplies a
repeatable offset from the native seed, player slot and word. Replay restores
the same delays. Timing does not consume or alter the vocabulary-selection hash.

This is a **partial pacing model**, not original AI equivalence. The original
common opponents also distinguish related-word attempts and unavailable words;
their selection, candidate lists and random stream differ from ours. The native
model recognizes only the documented plural/past groups; other related words
still receive base-word timing. Original
cursor travel and submission delays are not included; the existing native cursor
preview occupies the final second of a scheduled word. Mr. Boggle's cadence and
the common opponents' exhaustion policy still need separate reference checks.

Visible play submits at most one word per frame and schedules the next delay
from the actual submission. Background play follows the same delay sequence on
its continuous logical clock, catching up without depending on frame rate.
Per-player Chess clocks still exclude other players' turns. Exact deadlines,
early-finish rules, scoring and human-only high scores retain their existing
behavior. No new settings or game files are required.

## Provenance and validation

The source is the owner's read-only `Boggle.exe`, SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
An external interpreter harness executes the original Classic selection at
`0x42e0e0`, configuration at `0x42cda0` and timing at `0x42f070`, with invented
candidate lists and replacement CString, profile, random and clock services.
The original selection chooses an available base word; the harness does not
replace the selection or timing calculation. Path-reset and display services
are isolated from this test. Only behavioral facts are recorded here; the
original implementation, harness and raw results stay outside every Git worktree.

The measured contract matches 2,304 isolated original probes, spanning both mode
minima, visible/background flags, all three common levels, 1–12 pending words,
four successive base-word ordinals and four controlled random samples. Another
48 boundary/sequence observations cover the initial ranges and growth. Candidate
strings vary in length without changing the candidate-count behavior. These
checks do not execute full original turns or cursor movement.

Synthetic tests check measured bounds, scarcity, progression, deterministic
reset, invalid/failed builds, delayed frames, saturating clock arithmetic and
coarse/fine background scheduling. Supplied-data computer/background checks
validate legal submissions and stable timing metadata. This is the repository's
independent reimplementation workflow, without claiming researcher/implementer
separation or an end-to-end comparison with the original game.
