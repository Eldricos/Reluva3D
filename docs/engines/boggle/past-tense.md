# Common opponents' bounded past-tense follow-ups

Beginner, Intermediate and Advanced can now attach one selected past-tense form
to a base group, after its selected plural if there is one. Each word must already
be in the native opponent's sampled dictionary/path pool. No word or cube path
is synthesized, and Mr. Boggle keeps its separate longest-first plan.

## Measured cases

Isolated execution of the original common past-form classifier supports these
cases. They describe the program's bounded behavior, not general English grammar.

| Base ending | Supported follow-up |
| --- | --- |
| Final `E` | Append `D` |
| Final consonant other than `Y`, preceded by `A/E/I/O/U` | Repeat the final letter, then append `ED` |
| Final consonant other than `Y`, preceded by another non-vowel | Append `ED` |

Other vowel endings and `Y` forms are outside native group formation. Irregular
verbs, broader suffix rules and shared-prefix groups are not inferred. A form
that does not qualify can still be selected and played as an ordinary independent
dictionary word.

The original uses a difficulty-dependent probability gate to recognize a form.
The native model does not reproduce that gate. Original selection previews an
enabled form without consuming it, consumes it once on submission, and puts it
after an enabled plural in the same group. Its extra thinking phase is zero and
does not increase base-word thinking bounds. This does not establish complete
visible submission timing.

## Independent native integration

The existing [plural pairing](plurals.md) pass runs first and retains priority.
A second pass considers bases in increasing length, using native rank for ties.
It attaches at most one supported past form to each root, including roots that
already have a plural. A target already assigned to another group, or heading
an existing plural pair, is unavailable. Follow-ups cannot themselves become
bases. Groups do not chain recursively or share entries.

The resulting native order is base, optional plural, optional past form. Groups
retain their root's native rank. This construction and choosing every eligible
selected form are native policies, not original candidate ordering or strength.
The selected word pool is preserved exactly. Replay restores the same groups.

Each root incurs one [base-word thinking phase](thinking.md), using the remaining
group count and root ordinal. Each follow-up uses a native one-second presentation
interval, with or without Computer Cursor, and does not advance thinking bounds.
Visible play remains limited to one submission per frame; background play follows
the same logical schedule. Canonical word validation, scoring, shared-word
cancellation, deadlines and high-score eligibility retain their existing rules.
No settings or import files are added.

## Evidence and validation

The source is the owner's read-only `Boggle.exe`, SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
An external interpreter harness executes the original classifier at `0x42e9c0`,
selection at `0x42e0e0`, configuration at `0x42cda0` and timing at `0x42f070`.
It replaces MFC, random, clock and display services with invented strings/lists.

- 16,224 classifier contracts cover all 26×26 final-letter pairs, four candidate
  forms, all three levels and forced allow/deny probability gates. They check
  acceptance, rejection and the selected candidate pointer.
- 48 selection/timing contracts cover both Classic minimum lengths,
  visible/background flags, all three levels and four invented bases, including
  a Qu-like spelling. They verify preview, consumption, exhaustion, zero extra
  thinking and unchanged base-word timing bounds.
- 96 mixed-group contracts verify base/plural/past order with either plural
  append form, non-consuming previews and zero follow-up thinking.

These are isolated contracts, not full original rounds or original board-path
comparisons. Synthetic native tests cover both boards, legal Qu paths, dictionary
and sample exclusions, unsupported forms, group conflicts, replay, canonical
submissions and frame-independent scheduling. The supplied-data/import checks
validate every planned word and hash scheduled times. Original binaries, raw
observations and reference harnesses remain outside every Git worktree. The native
map-based grouping pass is independently written from this behavioral specification.
