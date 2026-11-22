# Installed word definitions

After a round, select a word in the results list and press **D**, or click
**Definition**. The native viewer shows matching articles from the installed
`Dicts/R016E613.DAT`. Up/Down, Page Up/Down, Home/End and the mouse wheel scroll;
Left/Right or Previous/Next switch matching articles. Escape, Enter, D or Close
returns to results without changing scores, replay state or approved words.

The archive is loaded and indexed on first use. Older imports remain playable:
re-import into a new external library folder, or copy the archive from your own
installation's `Dicts` folder into the existing imported `Dicts` folder.
Missing, corrupt and unmatched entries have a recoverable viewer message.
No original reference DLL is imported or executed by the engine.

## Supported behavior and limits

Lookup uses the archive's explicit ASCII lexical fields: main headwords,
alternate headwords, subentries, stored inflections and root derivatives.
Results follow that category order, with article IDs in archive order within
each category. Duplicate articles appear once. The status line identifies the
match type and parent article; etymological roots have their own heading.
Multiple articles can be browsed with Previous/Next.

The original game lowercases a requested word before calling its case-sensitive
reference API. Native lookup accepts either input case, then matches only
stored fields with that lowercase spelling. Capitalized-only abbreviations
and names no longer join ordinary lowercase results. Eligibility belongs to
each field: a lowercase alternative or subentry can still lead to a capitalized
parent article. Direct article-ID access continues to cover the entire archive.

No suffix rules or inferred stemming are applied. Original implicit inflections,
complete ranking/cross-reference behavior, and root links whose authored index
case differs from the displayed derivative remain open. Definitions remain
plain text with paragraph breaks; phonetic/font escapes are shown in brackets
and unsupported legacy characters as question marks. Original typography,
pronunciation glyphs and rich layout remain open. Membership and personal
dictionaries are separate; this change does not affect word acceptance or scores.

## Independent format implementation

The supported archive is 13,263,360 bytes, SHA-256
`49789292122be628e46bad04cec4142c23e6513ac19123c76110babf108b54d1`.
The entry component starts at 0x400. Its seven big-endian section descriptors
identify a little-endian page directory, two symbol tables, a literal pool and
three grammar sections. The native reader does not execute grammar sections.
The packed stream uses MSB-first 13-bit symbols without entry byte alignment.
Symbols expand to literals or pairs of child symbols; a four-FE-byte literal
terminates an article. The 64-entry page directory points to the preceding
terminator at each page boundary. All articles and directory boundaries are
validated before publishing the index.

The reader checks section bounds/overlaps, table dimensions, cyclic symbol
graphs, expansion depth, token cache and article/aggregate sizes. Unsupported
headers fail closed and clear previous state. Additional `<WD>` fields provide
alternate headwords; `<RUN>` fields provide subentries; literal `<INF>` fields
inside complete `<INFL>...</INFL>` groups provide inflections. `<DRV>` fields
are indexed only in articles beginning with `<IWD>`, the root heading.
Syllable dots are ignored in keys; the two observed stress escapes `bprime`
and `lprime` are removed only from subentry keys. Other escapes, non-ASCII,
multiword and punctuated keys are excluded. Field padding is trimmed; user
query syntax remains restricted to ASCII letters. Generic prose and reference
fields do not become lookup keys.

Each category and the deduplicated combined result are limited to 64 articles
per key. Total index links are bounded at 400,000. Synthetic fixtures construct
invented articles and malformed variants; no retail definitions, codebooks,
word lists or decoded entries are committed.

## Evidence and validation

Read-only original analysis links Boggle initialization at 0x432f10 and lookup
at 0x433360 to its reference DLL APIs. An isolated external interpreter opened
the owner's archive using read-only callbacks. REDLL entry decoding at
0x10048e80 and paging at 0x10049880 established the stream behavior. Original
instructions, modules, raw traces and decoded articles stay outside Git and
are never runtime dependencies.

The native reader validates all 94,693 articles. Its supported game-query index
filters fields by the game’s lowercase query spelling. Fifteen positive
reference queries yielded identical expanded
entry markup for the IDs returned by the original; one negative query matched
as absent. This corroborates stream decoding, not complete query or formatted
text parity. The current validation fingerprint covers every article plus the
lookup indexes (normalized keys and ordered article IDs).

The current indexes add alternate forms, subentries and etymological roots:

| Field category | Distinct ASCII keys | Key-to-article links |
| --- | ---: | ---: |
| Main headwords | 49,430 | 51,308 |
| Alternate headwords | 6,115 | 6,188 |
| Subentries | 21,699 | 21,740 |
| Stored inflections | 32,021 | 33,291 |
| Root derivatives | 3,756 | 3,855 |

Independent parsing of all 94,693 article streams verifies all 116,382 filtered
index links. There are 103,669 distinct lookup keys. The external round-trip
check validates all 114,303 returned articles and match labels. The index
checksum `eae44f27fe96ac4431a1d1385410bb00` matches an independent standard
MD5 calculation. The combined article/index fingerprint is
`e1a8fb1e82cb1c7640f59fb17a0378e2`.
The external game-query sample now contains 58 lowercase queries: 56 match the
original's complete ordered article-ID lists. The previously extra abbreviation
and proper-name results are removed. The two remaining differences are an
implicit plural and a root link whose displayed capitalization differs from
its original query-index spelling. No retail word exceptions are hard-coded.
This corroborates the supported field rules, not complete reference-library
search compatibility.

The executable's query loop at 0x4333b8–0x4333ec calls the import at 0x495270;
PE import inspection identifies it as `MSVCRT.dll!tolower`. Separate direct-API
case probes establish that differently capitalized queries can yield different
article lists. Root probes also expose the printed-field/index discrepancy.
Original query records are 23 bytes each, corroborated by the game's selection
address calculation at 0x4334e4–0x4334f1. Original modules and query records
remain external research inputs, never runtime code or repository fixtures.

Only definition metadata changes in the fresh-import record. Import selection
and existing game/audio/artwork records remain unchanged; no additional
original files or save migration are required.

`reluva-tool boggle definition-check <folder> [ASCII headword]` checks the full
archive or a selected lookup, printing only counts and checksums. Optional
`boggle.gamedata.definitions` and fresh-import checks preserve metadata only.
Synthetic tests cover decoding, rejection/reset, scrolling, match navigation,
focus/close event guards, unchanged scores and missing/corrupt archive recovery.
Lexical-field tests cover category priority, duplicate suppression, multiple
parents, stress/syllable markers, padding, root headings, incomplete groups,
excluded prose/reference fields, invalid keys, no inferred suffixes, checksums
and reset after failure. Case tests cover uppercase-only fields, lowercase
alternatives in capitalized articles, case folding of caller input, root fields
and continued direct access to capitalized articles. Scripted frontend checks
browse variant, subentry and
root results, then close/reopen and replay while preserving the final score.

The 44 applicable English Boggle checks pass, including a fresh imported copy.
The all-language dictionary and complete resource survey checks were excluded
because this validation workspace holds the restored Classic dependencies only.
Two external SDL software-rendered runs with the installed resources verify
the corrected case results in Original and Master, including opening, scrolling,
focus loss/return, closing/reopening, replay and unchanged scores. Earlier native
runs covered variant, subentry and root results in both modes.
Reader, viewer, frontend and import suites also pass ASan/UBSan (LeakSanitizer disabled for the execution tracer).
All 66 restored original files retain their manifest sizes and SHA-256 hashes.
The owner confirmed the initial definition viewer works. Later lookup
extensions have native checks; their detailed Windows coverage has not been
separately reported. The current case-filtering extension needs a Windows test.
