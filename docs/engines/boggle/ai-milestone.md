# Classic AI behavior milestone

This completes the U.S. English local Classic AI behavior milestone for Original
and Master, Traditional and Chess Timer, visible and background opponents.
The former native quarter/half/three-quarter vocabulary samples, hash ordering,
disjoint suffix pairs and fixed Mr. Boggle cadence have been replaced.

## Selection specification

Candidates come from legal board discovery order: first cube-index path for each
word, with a prefix before its extensions. Common opponents use `AI.dic` and the
verified nine-letter prefix boundary; Mr. Boggle uses the main English dictionary.
Personal additions do not expand either source.

Mr. Boggle retains the complete pool, longest first. Equal lengths retain board
discovery order. It does not use random hash ties or require `AI.dic`.

The common levels reorder candidates through repeated random relocations before
recognition. The measured relocation count is five times the pool size when
there are more than two candidates. The native implementation uses vector
relocations, independently of the original container representation.

Pools smaller than 15 use a separate fallback: each candidate's length threshold
is reduced by 400 per mille, with accepted candidates presented in reverse
reordered order. This route does not build related-word groups. Larger pools use
probability-gated suffix recognition, shared endings and shared prefixes. Roots
and related-root candidates in that route are at most seven letters; attached
forms still respect the discovered vocabulary and search boundary.

The measured U.S. English thresholds are per mille, with strict less-than tests:

| Level | Lengths 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 | Plural | Past | Shared ending | Shared prefix |
| --- | --- | ---: | ---: | ---: | ---: |
| Beginner | 650 / 300 / 100 / 0 / 0 / 500 / 250 / 500 | 500 | 250 | 500 | 200 |
| Intermediate | 700 / 500 / 250 / 100 / 0 / 700 / 400 / 600 | 700 | 400 | 600 | 300 |
| Advanced | 1000 / 1000 / 850 / 650 / 450 / 900 / 900 / 900 | 900 | 900 | 900 | 700 |

The length-8/9/10 entries matter to the small-pool fallback; they are measured
program behavior, not a general claim that longer words are easier. Negative
fallback thresholds cannot accept a word, but still consume a random sample.
Stronger levels are no longer promised to retain a weaker level's exact subset.

Plural/past recognition uses the observed append forms described in
[plural evidence](plurals.md) and [past-form evidence](past-tense.md). The first
eligible gated form is chosen from the current candidate order. The native model
does not invent an English stemmer or Y-replacement rule; expanded synthetic
Y/Qu cases agree with the original's outcomes.

Shared-ending recognition can seed additional groups and supplies a credit equal
to the matching ending length. Shared-prefix recognition visits longer prefixes
first, inserts recognized groups after the current group, and can extend newly
recognized groups. A root submits before its plural and past form. Recognition
gate order matters: initial/ending groups probe plural then past, whereas prefix
groups probe past then plural. Submission order remains plural then past.

## Thinking, movement and retirement

The [measured base-thinking bounds](thinking.md) remain in use. Related roots
receive a level-dependent credit rebate (1000/1200/1400 ms per credited letter),
with a 0–1499 ms variation, clamped to zero. Credited roots advance the minimum
but not the base random span. Suffix follow-ups have no extra thinking phase.
The first selected word starts its movement without an initial thinking phase.

| Level | Cube travel speed, pixels/ms | Submit travel speed | Cube hold | Between-word gap |
| --- | ---: | ---: | --- | --- |
| Beginner | 0.4 | 0.6 | 10–89 ms | 200–399 ms |
| Intermediate | 0.5 | 0.65 | 10–89 ms | 200–399 ms |
| Advanced | 0.6 | 0.7 | 10–89 ms | 200–399 ms |
| Mr. Boggle | 0.4 | 0.5 | 300 ms | 400 ms |

These are nominal 640-pixel-coordinate parameters. The native 800-pixel view
scales travel speed with its geometry. Press, release and submission boundaries
use 50 ms service intervals, including integer coordinate rounding on each
movement step. Dividing distance by speed and rounding only once is insufficient.
The cursor highlights a cube at its scheduled press and travels to the word list
before submission. Disabling visible tracing leaves the same logical schedule.

Mr. Boggle finishes on its last word. Common opponents finish after their final
empty search, with the measured 10–16.999 second search wait plus the between-word
gap and service boundaries. They no longer wait out a full timed round or retire
immediately on exhaustion. A timer can expire first. Initially empty plans retire
without this final-search wait. These rules apply to visible timed/untimed turns;
background words use the same logical schedule without a visible turn.

Visible play emits at most one word per frame and schedules subsequent work from
that actual submission. Hidden play catches up by logical timestamps independently
of service-frame cadence. Per-player Chess clocks exclude other turns. Existing
scoring, shared-word cancellation, review and human-only high scores are retained.

## Scope and remaining presentation differences

The native engine runs no original executable code. Its board generator, random
source and seed mapping remain independent. Reference comparisons inject the
same random samples into both implementations to compare decisions; they do not
claim identical native/original seeded rounds.

Reconstructed board geometry, initial pointer position, arrow appearance and
between-event interpolation remain native. Thinking is presented as waiting;
decorative idle pointer wandering is not reproduced. The nominal movement and
hold rules are verified separately from that presentation. Exact original camera,
idle wandering and frame-for-frame wall-clock/cursor matching belong to the
remaining presentation comparison, not a claim made by this milestone.

The owner should still test this combined stage on Windows. Other languages,
network opponents and other game modes remain separate work. No settings,
dependencies, import files or original-file writes are introduced.

## Provenance and verification

The source is the owner's read-only `Boggle.exe`, SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Original entry points include configuration `0x42cda0`, common planning
`0x42d260`, selection `0x42e0e0`, timing `0x42f070`, Mr. Boggle selection
`0x403010`, and service/movement `0x4015c0`, `0x403700`, `0x4039e0`.
External interpreter harnesses replace MFC, OS, random and board-search services
with controlled inputs; the original AI functions execute unchanged in memory.
Path discovery itself has separate reader/solver evidence.

- 6,144 whole common-plan comparisons: pool-size boundaries, both mode minima,
  three levels and 128 random streams; word order, relation credit and random
  consumption match.
- 1,920 extended suffix/Y/Qu/threshold comparisons and 192 comparisons using
  candidates from supplied boards/dictionaries also match.
- 128 Mr. Boggle ordering comparisons establish stable equal-length ties.
- 73 profile-parameter comparisons cover original configuration/constructor values.
- 5,376 nominal movement comparisons cover all levels, two origins, one-to-eight-cube
  paths, hold boundaries, visible/background flags and display scales.
- 72 empty-search retirement comparisons cover visible/background common players,
  gap boundaries and search waits.

Synthetic regression cases include the mixed-suffix gate-order discrepancy found
by the expanded probes, per-step movement rounding, retirement, geometry guards,
cursor press/release frames, replay and background schedule independence. Supplied
install/CD/import checks validate every planned word and record metadata only.
All original bytes, raw observations and research harnesses stay outside every
Git worktree. The implementation is independently written from behavioral facts;
this does not claim a formally separated two-team clean-room process.
