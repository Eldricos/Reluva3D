# Classic Original: comparison with the Windows original

This is a reference procedure, not a claim that the pending cases match.
Use the owner's original executable in the VM and the Boggle branch's native
build. Keep observed boards, word lists, captures and raw output in an external
research folder, outside **every** Git worktree. Do not add them to this guide.
Only concise behavior findings and synthetic regression cases belong in Git.

## Status and setup

The owner reports that the playable slice compiled in Visual Studio 2022,
launched and completed one round without issues. Original-game VM checks have
already confirmed three minutes, minimum three letters and the length-based
scoring table. English dictionary routine comparisons, scoring and adjacency
reference checks are described in [Classic evidence](classic.md) and the
[dictionary specification](formats/dictionary.md).

The first owner VM report confirms immediate scoring, loss of unfinished input
at expiry, no visible action on a repeated word, and a minimum-three-letter
notice for a two-letter attempt. It also established the workflow now
implemented in the native slice:
rejected words remain in red and the round-end UI offers to add them to the
user dictionary, with No to all / Yes to all / No / Yes choices.

| Behavior | Comparison status |
|---|---|
| Immediate scoring | Confirmed by owner; native submissions also score immediately |
| Unfinished input at expiry | Confirmed lost; native engine clears pending input |
| Repeated word | Original visibly does nothing; native score/list stay unchanged without duplicate feedback |
| Two-letter attempt | Both reject; original shows a notice, native uses inline feedback |
| Rejected-word history | Native keeps pending words in red; see the user-dictionary specification |
| User-dictionary prompt | Observed after the round; native four-choice review and per-target saves now implemented |
| Deferred points and eligibility | Owner confirms points count immediately, Yes keeps them, No removes them, and impossible board paths cannot enter the list |
| Submitted round total | Matched after the owner corrected one transcribed word: nine accepted words, nine points |
| Five- and six-letter scoring | Additional observed round matches: eleven accepted words, eighteen points |
| Qu spelling and letter-count scoring | Observed Qu round matches: seven accepted words, eight points; five letters using four cubes score two |
| Seven/eight-plus-letter play, bare Q and exact timing | Still require suitable observations |

The owner corrected one transcribed word and confirmed the corrected word was
accepted by the original. Replaying the corrected nine-word sequence on the
observed board accepts every word for one point each and finishes with nine
points, matching the reported original total. The earlier eight-point result
is resolved as a transcription error; no rule or dictionary change was needed.

Two additional owner VM rounds now also match native replay:

| Observation | Native accepted words | Original reported total | Native total |
|---|---:|---:|---:|
| First corrected round | 9 | 9 | 9 |
| Round exercising five/six-letter words | 11 | 18 | 18 |
| Round containing a Qu cube | 7 | 8 | 8 |

The second round exercises three-, four-, five- and six-letter words. The Qu
round includes a five-letter word using four cubes: native scoring awards two
points for its five letters, reproducing the reported total. Counting cubes
instead would produce seven points for that round. This corroborates the
original executable analysis and existing synthetic multi-letter-face tests;
no implementation change was needed. Original per-word credit and input method
were not separately reported. Bare-Q rejection and keyboard-versus-mouse input
behavior remain untested in the original.

Actual submission timestamps were not reported. Replay times were assigned
only to place submissions safely before expiry, not measured. Duplicate and
minimum-length probes in the replay were synthetic additions; only their
general observed behavior was supplied. The original board, word list,
correction details, detailed paths and raw output remain outside Git.

Final-score equivalence for these three observations is **confirmed**. The
rejected-word/user-dictionary workflow is now implemented; see
[its evidence, persistence contract and scope](user-dictionary.md).
Seven- and eight-plus-letter play, bare-Q behavior, input-method differences
and precise timing still need suitable observations. These comparisons do not
establish full original-game compatibility.

Select U.S. English, Classic, Original, one human player, and no AI/opponents.
Keep the default three-minute limit and three-letter minimum. Use a fresh
profile or record any custom dictionaries/options; do not silently compare an
altered dictionary. Use the same supplied file set for native replay.

Do not try to match random seeds: the prototype deliberately uses a different
shuffle. Instead record each original board left to right, top to bottom, and
replay that board through the native model. Record a Qu cube as **one token**
`Qu`, not separate Q and U cubes. Keep a stable board orientation in the VM.

## First pass: one ordinary board

Record the 16 faces, submissions in order, approximate elapsed times, visible
response and final credited points per word. Distinguish a word that is typed,
a word entered into a candidate list, and a word finally credited. The native
prototype judges immediately, as the owner now reports for the original.
Continue distinguishing candidate input from credited words in later checks.

| Case | What to try | What to record |
|---|---|---|
| Valid word | A familiar word formed by touching cubes | When it is judged, credited points, score change |
| Duplicate | The same credited word twice; change case if the UI permits | Second result, whether it changes the final score/list |
| Minimum | A two-letter word present on this board | Input/submit behavior and final credit |
| Dictionary rejection | A pronounceable invented string that has a valid board path | Visible rejection stage; native dictionary result may explain it |
| Board rejection | A familiar dictionary word that cannot be traced | Visible rejection stage and final credit |
| Diagonal | A valid word requiring a diagonal step | Credited result |
| Reuse/wrapping | A dictionary word possible only by reusing a cube or crossing a row edge | Rejected result, distinguishing it from dictionary rejection |
| Score lengths | Valid words of lengths 3, 4, 5, 6, 7 and 8+, when available | Per-word points and final total |

A random board may not offer every case. Mark unavailable cases **not tested**
and use another round; do not force a conclusion from an unsuitable board.
Some input interfaces may prevent an invalid path before submission. Record
that as input behavior, rather than pretending it was a dictionary rejection.
For each accepted word retain its cube path in external notes if ambiguous.

## Second pass: Qu

When an original board contains Qu, record it and try an available valid word
using that cube. Check typing and cube selection separately if supported.
Record the resulting spelling and points. Qu should be investigated as one
cube contributing two letters; the native model already implements that rule.
For example, an independently chosen five-letter word using four cubes would
separate letter-count scoring (two points) from cube-count scoring (one point).
Do not claim the scoring case was tested if only a four-letter word was found:
three and four letters both score one. If possible, also try a dictionary word
requiring a bare Q without U; record whether input prevention or word rejection
occurs. Do not rely on invented words to test this distinction.

## Third pass: expiry and final score

Use a separate uninterrupted round with a short, recorded submission list.
Leave a partly typed word when the timer expires. Record the last displayed
time, whether editing/submission remains possible, what happens to unfinished
input, when results appear, the credited list, and final score. Keep the VM
focused and running normally for this pass; a suspended VM is not a valid
wall-clock timer measurement.

For accepted words verify the final total using 3/4/5/6/7/8+ letters =
1/1/2/3/5/11 points, with each credited word counted once. Record any penalties,
bonuses, delayed adjudication or different duplicate behavior instead of
assuming the prototype's total. Test loss of focus and any original pause
control in a separate round. The prototype currently keeps time while unfocused.
A second-granularity display and human input cannot establish an exact
millisecond cutoff; native tests check the exact 180,000 ms boundary separately.

## Replaying an observation locally

The command below runs the native **model**, not the original or the graphical
input interface. It loads the owner's English dictionary and a supplied board,
then submits words in order with an injected clock. It does not use the native
shuffle. It does not compare against VM results automatically or certify a match.

Create `C:\BoggleResearch\round.txt` (outside the repository). The first four
nonblank/noncomment lines must each contain four space-separated faces. Later
lines contain `<elapsed milliseconds> <word>`; `-` advances time without a
submission. Times are measured from round start and must be nondecreasing.
Blank lines and whole-line `#` comments are allowed. No inline comments or
cube-path arguments are accepted. The command does not load native user words
or resolve dictionary choices: an unknown word on the board now has provisional
points and stays pending. Its score is not final until review in the game. A single file represents a single round, without pause or
replay. Limits: 64 KiB/file, 256 characters/line, 256 steps, 64 bytes/word and
elapsed times through 3,600,000 ms. Keep optional explanatory notes separately.

This example is an **invented board and trace**, not retail data or a VM result:

```text
C A T S
X Qu I T
B D E F
G H J K

1000 cat
2000 CAT
3000 ab
4000 quit
179999 cats
180000 tac
180001 -
```

On Windows, substitute the actual build and game locations:

```powershell
& 'C:\ReluvaBuild\Release\reluva-tool.exe' boggle classic-replay 'C:\Games\Boggle' 'C:\BoggleResearch\round.txt'
```

Each output row gives its step number, supplied time, native submission result,
points added, running total, accepted count, phase, remaining milliseconds and pending-review count.
It omits submitted words and board faces. A successful exit means the scenario
ran, **not** that VM behavior matched. A valid word at exactly 180,000 ms must
produce `not-playing` with the score unchanged in this native model. Use `-`
at that time if the VM did not actually receive an attempted submission.
Do not invent human event timestamps to turn a coarse observation into a precise
measurement. Approximate observations well away from expiry can be replayed;
mark near-deadline timing inconclusive until captured more precisely.

## Reporting findings

Send the observed board and ordered submissions with accepted/rejected/pending
status, per-word credit and final total. Identify which original input method
was used. Include the build commit, mode/options, and any timing uncertainty.
Board/word details can stay in external research notes or this conversation;
repository updates should contain behavior findings and independently invented
regression fixtures only. Mark each checklist case passed, mismatched,
inconclusive or not tested. Fix a discrepancy only after distinguishing rule
behavior, dictionary membership, input handling and adjudication timing.
