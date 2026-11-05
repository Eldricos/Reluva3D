# All Words and missed-word review

After the round and any dictionary decisions finish, choose **All Words / A**.
The list switches to an alphabetical vocabulary available on that board. Counted
submissions keep their existing appearance; missed words are blue and their
points are potential points only. The final score and counted-word total never
change. The artwork panel labels its larger list ALL, rather than presenting
its size as the number submitted.

Click a row or use Up/Down to inspect a path. A missed word is explicitly marked
not submitted. Counted words keep the path accepted during play, including a
manually selected alternative path; missed words receive one legal native path.
Qu is consumed as one whole cube, and its two letters count toward length and
points. Page keys, arrows and wheel browsing work on the current list.
Right-click clears path selection while retaining the results viewport.

Choose **Counted Words / A** to return to the submitted list. The discovered
vocabulary is cached for that finished board. Replay and New Board clear it,
and the control is inactive during play, transitions and dictionary prompts.
No discovered word is submitted, added to the dictionary, or awarded points.
An empty submitted list can still reveal available words while retaining a
zero final score.

The vocabulary includes playable ASCII English entries from `English.dic` and
approved words from the native per-target English user dictionary. Master's
four-letter minimum is applied independently of Original's three-letter minimum.
The list is complete relative to those sources and supported rules, not every
possible spelling or another language's dictionary.

## Independent bounded search

The native solver first filters the lexicon by minimum length and available
letter counts. It explores touching, unused cubes while narrowing a sorted
candidate range by the complete face prefix. A multi-letter cube is appended
atomically. Duplicate paths produce one entry; exploration can stop when every
eligible target has been found. Output is sorted and contains one legal path
per word. Counted submissions override discovered paths in the review view.

The search admits at most 50000 eligible candidates and 500000 path visits,
with recursion bounded by the 16/25 cubes. Existing dictionary-reader limits
bound the input lexicons. A limit failure clears the search output and leaves
the counted view and score intact. Partial discoveries are never displayed as
a complete list. Difficult future boards may therefore report a limit error;
this is an explicit bounded implementation, not a claim that every possible
board completes within the budget.

This is independently written prefix-search code. No original or third-party
solver implementation was translated or copied. Neither gameplay nor the
metadata tools export the retail vocabulary to files.

## Evidence and validation

The original English word-review dialog 15700 at DLL file offset `0x3104`
contains Show All Words control 2. DLL identity is recorded in the
[install manifest](../../plans/boggle-1997/install-manifest.tsv).
This establishes the original feature's presence. Native button placement,
A shortcut, colors, path choice, caching and saved-word inclusion are native
policies; exact original filtering/navigation remains to be compared in the VM.
Definitions and the original dialog layout remain unfinished.

Synthetic tests cover complete sets, deduplication, saved words, minimum lengths,
no wrapping/reuse, whole Qu faces, repeated-letter boards, zero-result searches,
budget failures after partial discovery, score-preserving toggles, kept paths,
empty rounds, paging, viewport preservation and replay/new-board resets. Tests
use invented dice and generated vocabulary; records contain only checksums.

`reluva-tool boggle all-words-check <folder>` compares discovery against an
independent word-by-word `Board::find` scan, validates every path and reports
only counts/checksums. It excludes private per-target words for reproducibility.
The two deterministic native initial boards yield 374 words in total, checksum
`8422ca959f8ecd50164ea97972c4c274`, with matching independent counts. The optional
allwords and fresh-import checks run this against the owner's files outside Git.

All 28 Boggle CTest checks pass with the supplied install. The normal suite
passes 38 checks and skips 23 unconfigured optional data checks. Solver/results
checks and actual-file discovery pass ASan/UBSan (LeakSanitizer disabled for the
execution tracer). Actual-resource native SDL previews cover both variants;
captures remain outside Git. The new UI still needs the owner's Windows test.
No new assets, re-import or launcher settings are required.
