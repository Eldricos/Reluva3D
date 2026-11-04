# Completed-round review

After a round ends and the existing closing clip finishes, Classic now shows
the completed board and submitted-word list. Pending dictionary decisions remain
modal. Only when all decisions are resolved does a final score/word-count summary
appear. Declined provisional entries contribute no row or points; approved user
words retain their points. This applies to Original and Master, timed/manual
endings, installed artwork and the prototype view.

Click a counted word, or use Up/Down, to review it. The word and its points appear
below the board, the row is highlighted and numbered cube badges show its stored
path in order. Qu remains one numbered cube with two letters for scoring. The
review uses the path accepted during play, rather than solving for a different
path after the round. Full-page keys, arrows and wheel history browsing remain
available. Keyboard word selection scrolls its row into view.

Right-click restores the summary. Enter / Replay starts the same board again;
F2 / New Board creates the next board and returns to Ready. Menu / F3 retains
the existing menu route. Word input and cube entry are inactive during results;
reviewing never resubmits or changes points. Expired unfinished input is still
discarded. Results must be displayed before a queued Enter can replay, including
when the last dictionary decision and Enter arrive together.

A results snapshot is published only from a finished round with no pending
words. It owns its kept rows/paths and checks their point total against the
round score. Replay and New Board clear it and any review selection. An empty
round has a zero summary and no selectable rows. History arrows take precedence
over the bottom row, so clicking them cannot accidentally select a word.

## Original evidence and native presentation choices

The inspected executable SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
English DLL identity is in the
[install manifest](../../plans/boggle-1997/install-manifest.tsv).
Its extended dialog 16008 at file offset `0xa62c` contains player name/score
fields 15400..15403 / 15410..15413, a Game Over heading, OK and View Highscores.
The end routine at `0x41af90` constructs it through `0x439fa0` near `0x41b3f6`
and invokes the modal dialog near `0x41b46c`.

Standard dialog 15700 at file offset `0x3104` provides a separate word list,
player columns, Show All Words and a definition field. These resources establish
a score-summary and word-review distinction. They do not by themselves establish
exact original screen order, lid state, word selection actions or keys.
The setup routine at `0x41a380` uses the close/raise helpers; that is not evidence
that the original reopens the lid at round end. VM corroboration remains pending.

The native implementation deliberately keeps the board visible using the open
scene and offers an inline final summary with direct replay/new-board controls.
It does not replay a raise-lid animation after closing. This is a native review
layout, not a recreation of the original modal Game Over/word dialogs. Cube
badges, keyboard selection and right-click summary are native controls.
Highscores, missed/unsubmitted dictionary words, INSO definitions, multiple
player columns and exact original dialog behavior remain outside this pass.
No original implementation was translated or copied.

## Validation

`boggle.unit.results` checks finalization guards, approval/decline scoring,
Qu paths, immutable snapshots, page/row boundaries and arrow precedence.
Scripted rounds in both variants and renderers cover timeout, discarded input,
blocked pending decisions, final-score visibility, equivalent mouse/keyboard
selection, read-only results, empty rounds, replay and New Board. Existing
records changed only where completed-round rendering changed. Fixtures and
vocabulary are independently invented; no retail words, boards or pixels are
embedded.

All 26 Boggle checks pass with the supplied install. The normal full suite passes
37 checks and skips 22 optional data checks without configured paths. Result,
history and layout tests, plus the actual-resource artwork checker, pass
ASan/UBSan (LeakSanitizer disabled for the execution tracer). External SDL dummy
runs render actual Original/Master summaries and numbered paths; captures stay
outside Git. The new native review still needs the owner's Windows test and VM
comparison. No re-import, new assets or new settings are required.
