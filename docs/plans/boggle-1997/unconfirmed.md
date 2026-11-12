# Boggle: outstanding evidence and limits

Companion to [inspection.md](inspection.md). These items are not verified
by the static inspection and must not become undocumented assumptions in code.

- Exact retail edition, patch level and original CD pressing. A 1997 PE
  timestamp and December 2000 copied-file timestamps are not release proof.
- The owner can run the original in a Windows VM and has confirmed Classic
  Original's timer, minimum length and scoring. Broader input, pause presentation,
  graphics, timing and network observations remain needed. See
  [Classic evidence](../../engines/boggle/classic.md).
- `cd/data.z` contents and SHA-256. It exceeds the executor's 32 MiB transfer
  limit. No compressed installation archive was extracted or installer run.
- Full registry/CD lookup contract, marker semantics and behavior without
  the original CD. Strings such as `SourceDirectory` are research leads.
- Multilingual normalization and Spanish legacy-byte lookup aliases; AI and
  multilingual user dictionary behavior. English deferred review and native
  persistence now exist:
  [evidence and limits](../../engines/boggle/user-dictionary.md).
  Binary `.dic` structure and English membership
  are now validated: [dictionary specification](../../engines/boggle/formats/dictionary.md).
- INSO `.DAT` layout, definition rendering and DLL ordinal contracts.
- Exact purpose of duplicate or unused-looking dictionary files. Determine
  the files the selected language actually opens before pruning an import.
- Original generator seed and shuffle equivalence. Native play uses an
  independent generator with fresh seeds; Classic adjacency, no cube reuse and
  complete multi-letter faces are confirmed in executable analysis.
- The five modes' board sizes, rules, scoring, timers, difficulty, player
  counts, camera behavior, animations and AI behavior.
- Save/high-score persistence, original user-dictionary editing/settings,
  multilingual behavior and original import/editor interoperability. Deferred
  scoring and path eligibility are corroborated by the owner in the VM.
  Native English user words persist separately from the original installation.
- Network wire protocol, DirectPlay/Third-i session model, host authority,
  timeouts, discovery, synchronization and interoperability.
- The separate title-sequence paths `Bitmaps/Title1.bmp`, `Bitmaps/Title3.bmp`,
  `Bitmaps/hasbro1.bmp` and `Bitmaps/hasbro3.bmp` are absent from both supplied
  expanded trees. `GS1CS.bmp` was incorrectly listed as absent earlier; it is
  present in both manifests and copies. See [frontend evidence](../../engines/boggle/frontend.md). Determine whether they are unused paths, require other
  packaging/extraction, or reflect incomplete resources before promising
  original menu/intro behavior. Present-file reader checks do not resolve this.
- Classic roll/lid and WAV playback now exist; the owner has corroborated startup,
  selection, writing, final-countdown ticking and expiry ringing in the VM.
  A subsequent VM check confirms distinct seven- and eight-letter cues replacing
  Writing; native Windows music/effects playback also passed the owner's test.
  Precise sequence timing still needs comparison. Full original overlays,
  full original frontend dialogs remain unsupported. Indexed timer AVI now decodes;
  the native Classic layout uses installed panels with approximate cube geometry.
  Cinepak intro and available mode-button animation now exist; exact original
  hover triggers, menu sound and audio/video latency still need comparison. Native selection and Classic setup now exist with installed
  selection artwork and a one-human Players confirmation. The owner has confirmed
  the original screen order and Escape behavior; the native post-intro stage
  order and Escape handling now follow those observations. Exact layout and
  full player configuration still need implementation/comparison. See
  [presentation evidence](../../engines/boggle/presentation.md).
- Exact UI layout and appearance across resource DLLs, fonts, palette modes
  and display settings. Successful media decoding is not a rendering match.
- A proven minimum runtime file set. The inspection proposes a conservative
  resource set; only dependency tracing and end-to-end runs can establish
  sufficiency for all original features. A tested Classic import set now covers
  the native English Classic Original/Master slice and existing detection markers
  (53 files with Classic/menu artwork and handwriting font, or the earlier 39/40/45/46/52-file subsets);
  the owner also confirmed that CD importing works. See
  [import scope](../../engines/boggle/import.md). It is not a full-game minimum.

Research evidence must cite a manifest SHA-256 and file offset/RVA or a
repeatable observation. Keep retail assets, dictionary entries, screenshots,
decompiler output and raw logs outside Git. Record confirmed results in the
inspection/specifications and remove an uncertainty only when evidence resolves it.

- WAV background music now supports the fifteen installed tracks and an independent
  launcher selector. Original CD-device music remains unsupported. Track timing
  and continuity still need VM listening checks; see [music evidence](../../engines/boggle/music.md).

The next presentation comparison is the new [Classic layout](../../engines/boggle/layout.md):
verify timer/word/score positions in both variants, then refine calibration and font. Native dice now use a consistent
projective plane with perspective-correct texture sampling. This does not block the existing playable Classic slice.
The owner has now confirmed both startup movies play.

The owner confirmed the installed Classic artwork works on Windows. The next
projection refinement retains measured tray corners; exact original camera,
the exact original orientation generator and fonts remain unverified.

The owner confirmed the projective dice refinement works on Windows and
observed the original auto-orient setting: aligned cubes when enabled, quarter
turns permitted when disabled. Native orientation settings now implement that
display choice. The original default and exact random-turn assignment still need verification.
The owner subsequently confirmed random orientations at start and no manual
rotation control; native interaction follows that behavior.

The owner confirmed the orientation update works. The next font pass adds
installed KPSI handwriting to the Classic word list with a native ASCII
rasterizer; exact original text sizing, weight and layout still need comparison.
See [font evidence and limits](../../engines/boggle/fonts.md).

The owner confirmed the handwriting update works. Native word-history arrows
now follow the measured original one-row direction/step behavior, with bounded
ranges, full-page keys and wheel support. Original font-dependent list capacity
and exact wheel/key behavior remain unverified. See
[history controls and evidence](../../engines/boggle/history.md).

The owner confirmed the word-history controls compile and work. The next native
Players step adds one human name with isolated persistence; original name-entry
workflow, original player database and multi-player/high-score associations
remain unverified. See [player naming and evidence](../../engines/boggle/players.md).

The owner confirmed name changing works. The native completed-round review now
shows final kept-word totals, selectable stored paths and direct replay/new-board
controls. Original Game Over dialog 16008 and word-review dialog 15700 are
identified, but exact original navigation and end-of-round lid state still need
VM corroboration. The setup routine's close/raise sequence is not evidence of
round-end behavior. See [results evidence and native scope](../../engines/boggle/results.md).

The owner confirmed native final scores work. All Words now discovers playable
English and saved custom words after adjudication, with potential points kept
separate from final score. Dialog 15700 establishes the original feature's
presence; original filtering, view navigation and INSO definitions remain
unverified. See [all-word discovery and limits](../../engines/boggle/all-words.md).

The owner confirmed the All Words update works. The native high-score table
now records finalized positive round scores in isolated storage. VM checks
confirm one shared Classic board across versions/durations and qualification
for manually ended rounds. A subsequent VM check confirms repeated names are
allowed and the board holds ten scores; native storage now preserves each
qualifying round, including repeated names and identical scores. Tie and zero-score qualification now match isolated original insertion checks;
original replay UI eligibility and score-file persistence remain unverified. See
[high-score scope and evidence](../../engines/boggle/highscores.md).

The owner confirmed the corrected ten-score table works, including repeated
player names. Normal Classic launches, menu re-entry and New Board now use
fresh native seeds; Replay retains its board/orientations. Tests and resource
tools retain explicit seeds. Original RNG equivalence remains unverified. See
[board randomization scope](../../engines/boggle/randomization.md).

The owner confirmed fresh boards and Replay work. The next Classic player
stage has an independent final competition-scoring model: shared words cancel
for every owner, after dictionary decisions finish. Original routines passed
16,384 isolated synthetic score comparisons, and four player controls plus
four opponent levels were identified in the DLL/caller. Live player turns,
AI timing/strength, the Traditional/Chess Timer options and full configuration
remain unimplemented and need VM corroboration. See [competition evidence](../../engines/boggle/competition.md).

Owner VM corroboration now confirms four player dropdowns containing previously
used human names and Beginner/Intermediate/Advanced/Mr. Boggle opponents, plus
end-of-round cancellation of shared words in human/human and human/computer
games. Original name-history restart persistence, player-turn sequencing and
AI timing/strength remain unresolved. See [confirmed player observations](../../engines/boggle/competition.md#owner-vm-corroboration).

The owner confirmed the competition foundation compiles and works. Native Players
now recalls saved human names through a dropdown, with a bounded per-target
most-recent history and version-1 profile migration. Original history lifetime,
ordering/capacity and on-disk format remain unverified. Additional live player
slots, computers and their turn/timing behavior remain future work. See
[human selection and native history policies](../../engines/boggle/players.md).

The owner has now confirmed the broad turn modes: Traditional gives each human
or computer a whole-round turn, while Chess Timer advances after each word.
Concurrent computer play and computer cursor movement are separately selectable.
Precise timer accounting, rejected/repeated-attempt turn boundaries and computer
strength/cadence remain unverified; the live turn controller is not implemented.
See [owner turn-mode observations](../../engines/boggle/competition.md#owner-vm-corroboration).

Live Traditional Classic now supports one to four local humans, with session
roster selection, full per-player rounds on the same board, hidden handoffs,
shared-word review and atomic final score batches. Broad turn/scoring rules
follow owner VM observations. Original multi-player dictionary-review timing,
name-slot edge cases and exact transition dialogs remain unverified; computer
opponents and Chess Timer are not implemented. See [Traditional scope and
validation](../../engines/boggle/traditional.md).

The owner confirmed Traditional works, then clarified Chess Timer: each player's
clock stops during other turns; repeats pass without adding another entry;
unknown words wait for review; short words show a notice; and expired players
do not end the match, with a finished/next-player notice on later rotation.
Local-human [Chess Timer](../../engines/boggle/chess.md) now implements this flow.
An external harness also matches 4,608 original positive-budget pause/resume
observations. Exact animation/dialog timing, no-path/short-attempt turn boundaries
and multiplayer review scheduling remain open; AI is still unimplemented.

The owner confirmed Chess Timer compiles and works. The next stage adds playable
turn-based Beginner, Intermediate, Advanced and Mr. Boggle opponents, including
all-computer rosters, using an independently written native strength/pacing
policy. Both turn controllers retain shared-word cancellation; computers stay
out of human history and the high-score table. New imports include `AI.dic`.
Original AI cadence/strength, exhaustion behavior, background play and cursor
movement remain open. See [computer evidence, limits and checks](../../engines/boggle/computers.md).

The owner confirmed the first computer-opponent stage compiles and works. A VM
check also established hidden background participation in both Classic turn
modes, final computer score then word-list presentation, and high-score
exclusion. Native [background play](../../engines/boggle/background.md) now adds
an independent Players switch and keeps computers out of the human rotation.
A further owner check confirms a continuous computer budget through switches,
visible current scores, full-budget computer completion when all humans end a
timed game early, and stopping with the last human in untimed play. The native
implementation incorporates these corrections. Original AI strength/cadence,
precise initial-start and intermediate dictionary-review timing remain
unverified. Optional visible computer cursor movement remains future work.

The next stage adds [optional visible computer cursor movement](../../engines/boggle/cursor.md)
for both boards and turn modes. It previews legal cube paths without changing
word plans, scoring or submission timing, restores the user pointer across
handoffs/exits, and stays inactive in background play. The original `MoveCursor`
profile references corroborate the option. The owner then confirmed ordered cube visits followed by moving to the word
list to submit in both turn modes, plus word-list click submission for humans.
Both are implemented. Exact highlighting, input progression and sound still
need comparison; interpolation and arrow appearance remain explicitly native.

The owner confirmed the cursor/list-submission stage works. The next pass
corrects [AI vocabulary and Classic search limits](../../engines/boggle/computers.md):
the first three levels use the auxiliary dictionary and a nine-letter prefix
boundary, while Mr. Boggle uses the complete main-dictionary board vocabulary.
Eight isolated original entry-point probes and six recursive boundary probes
agree, including the final-Qu edge. The former native minimum+N caps are removed;
sampling, ordering and pacing remain native, and full original AI equivalence is
not claimed. Mr. Boggle-only rosters no longer require the auxiliary file.

The owner then confirmed that Mr. Boggle tries longest words first and ends when
all its words are found in timed and untimed play. This pass also implements
those rules, avoiding an extra empty Chess turn after its final word.
Common-opponent sampling/order, Mr. Boggle equal-length tie order and all thinking
cadences remain native policies; common-opponent exhaustion needs a separate
reference check.

The next pacing pass measures the common opponents' base-word thinking delays.
An isolated harness runs original selection/configuration/timing with invented
candidate lists: 2,304 timing contracts match, covering level, candidate-count
scarcity, successive base-word attempts, Classic minimum lengths and background
flags. The native planner now applies these measured bounds to its retained
words with repeatable native jitter. Its sampled lists and related-word behavior
still differ, so this is not full original AI cadence. Mr. Boggle retains its
native one-second interval. See [thinking-time specification and limits](../../engines/boggle/thinking.md).

The owner confirmed the base-word pacing stage works. The next bounded pass
adds selected `S`/`ES` [plural follow-ups](../../engines/boggle/plurals.md) for the
common opponents. Ninety-six isolated original contracts verify selection,
non-consuming preview, consuming advancement and zero extra thinking; two more
verify first-eligible-plural classification. Native disjoint pairing preserves
the selected legal-word pool and uses a one-second presentation interval.
Original probability gates, full candidate order, other suffixes/shared-prefix
groups and exact cursor/submission timing remain open. Mr. Boggle is unchanged.

The next stage extends common groups with [bounded past forms](../../engines/boggle/past-tense.md):
append `D` after final `E`, append `ED` after measured consonant endings, or
double a final consonant before `ED` when the preceding letter is a vowel.
16,224 isolated classifier contracts, 48 selection/timing contracts and 96
base/plural/past ordering contracts match the original. The native grouping
pass preserves prior plural pairs and the selected legal-word pool, with one
thinking phase per root and native follow-up presentation intervals. Other
suffixes, shared-prefix groups, probability gates, full candidate ordering,
original cursor/submission cadence and Mr. Boggle timing remain open.


The Classic AI behavior milestone now replaces native vocabulary samples/hash
ordering, suffix-pair construction and fixed cadence with measured difficulty
gates, small-pool fallback, suffix/ending/prefix recognition, stable Mr. Boggle
ties, nominal travel/hold schedules and empty-search retirement. Whole-plan
comparisons include supplied-board candidates. See [the milestone specification](../../engines/boggle/ai-milestone.md)
for evidence and closure scope. Native randomness, geometry, idle-pointer
presentation and exact whole-round VM timing remain documented differences;
other languages and network play remain separate milestones.


The [Classic gameplay edge pass](../../engines/boggle/edges.md) now freezes
foreground/background clocks on focus loss, defers multiplayer dictionary
review until all players finish, shares approvals across subsequent reviews,
expands keyboard Q to Qu, and corrects tie/zero-score qualification. Short/no-path
Chess turn behavior and manual pause commands were subsequently corroborated
by the owner: too-short attempts keep the turn and show a notice; impossible
words cannot enter through either input method; Pause has no shortcut, and
minimizing/switching away pauses. Native incremental entry and mouse-only
Pause/Resume now implement those contracts. Exact keyboard path choice, pause
UI placement and audio pause behavior remain unverified.


The owner confirmed the live keyboard constraint, minimum-length notice and
manual-pause update compiles and works on Windows. The next
[presentation fidelity pass](../../engines/boggle/presentation-fidelity.md) uses
installed raised/pressed computer cursors and records measured camera/grid
contracts. Camera projection, animated cube overlays and exact dialog behavior
remain separate comparison work; original pause audio and round-end lid state
are pending VM observations.
