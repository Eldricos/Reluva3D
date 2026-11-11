# Classic high scores

After the round and dictionary review finish, **High Scores / H** opens the
native Classic table. Enter, Escape, H or Close dismisses it; closing consumes
the remaining events in that input batch so it cannot also start a replay.
The table lists rank, player name, Original/Master version and score. Quit and
Return to Launcher still work while it is open.

All Classic versions and round lengths share one table, including manually
ended and untimed games. The owner verified the shared Classic grouping and
manual-end qualification in the original VM. Other game modes remain unsupported.
Only the final score after all dictionary decisions can be recorded. Declined
words, unfinished input and missed-word discovery never contribute points.
Traditional local-human matches now submit all qualifying nonnegative final human scores in
one atomic batch; failed saves leave the whole batch uncommitted.
Recording happens once when finalized results are published; browsing results
or opening the table does not submit another score.

The table keeps the ten highest nonnegative **round scores**, allowing repeated
player names and identical score rows. The owner verified repeated names and
the ten-entry limit in the original VM. Each qualifying finished round adds
its own entry rather than replacing that player's earlier result. A replay is
a new round and can earn another entry; viewing results and retrying after a
successful save cannot. Renaming a player does not rewrite historical names.

New tied scores precede older ones and can displace the tenth entry. Zero
scores qualify when space or ranking permits. These insertion rules match 616
isolated original contracts; see [edge evidence](edges.md). Replay is treated
as a new completed round by the native controller. Original dialog 16007 also has ten name/score/skill rows.

## Storage and recovery

The launcher target's separate save directory contains `HighScores.txt`.
It is an original native format, independent of original game score files:

```text
RELUVA BOGGLE HIGHSCORES 2
original\t180\t12\tExample Player
```

The example uses literal `\t` notation for tab separators. Rows contain
version, configured seconds (zero means untimed), nonnegative score and the native
printable-ASCII player name. Loading also accepts version 1 files with their original positive-score constraint;
the next successful write uses version 2. Loading accepts LF/CRLF, at most ten score rows
and 4 KiB. Invalid numbers, modes, durations, names and excessive row counts reject
the whole file. No retail data or original score-file interpretation is involved.

Writes use `.tmp` and `.bak` siblings, check stream completion, replace through
a backup and restore that backup on a failed replacement where possible. A
missing main file can recover from `.bak`; an unfinished `.tmp` is not loaded.
A corrupt main file is not silently overwritten. File symlinks and non-files
are rejected; the canonical save directory must be outside the original game
folder. This is local single-process persistence; concurrent writers and
power-loss durability are not guaranteed.

A load/save error leaves gameplay and the final score available. The results
hint directs the player to H for the error and **Retry / R**. Retry reloads the
table and attempts the same finalized result; it does not bypass validation or
destroy a corrupt file. With no configured save directory, scores last only for
the current Classic game session. Tables are per launcher target, not shared
across separately imported copies.

## Evidence and validation

Read-only inspection of the supplied English DLL found standard dialog 16007
with name controls 15400–15409, score controls 15410–15419 and skill controls
15420–15429. Game Over dialog 16008 offers View Highscores. Executable/DLL identity
and the Game Over caller evidence are recorded in [results.md](results.md).
The owner confirmed in the VM that all Classic versions/durations share the
same board, manually ended games qualify, repeated names are allowed and the
board holds ten scores. The native implementation was
written independently; no original routine or score data was copied.

`boggle.unit.highscores` tests shared grouping, repeated names/identical rows, ties, top-ten
limits, restart, session-only storage, strict parsing, interrupted writes,
backup recovery, source-folder/symlink protection and failed-write preservation.
Scripted Original/Master rounds with both renderers check adjudication before
recording, untimed/manual endings, modal controls, unchanged discovery scores,
retry after a corrupt file, repeated replay scores, no duplicate recording
from successful-save retries and global exit cleanup.

The gameplay edge pass adds regression coverage for newest-first ties, zero-score
rounds and version 1 migration. See [validation and remaining reference checks](edges.md).
All 737 supplied installed files retain their manifest hashes. Existing native
score files remain readable; no reset or game re-import is needed. Windows
owner testing of this stage remains pending.
