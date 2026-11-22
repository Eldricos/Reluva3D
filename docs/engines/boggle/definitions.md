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

No suffix rules or inferred stemming are applied. Query case filtering and
complete original ranking/cross-reference behavior remain open: the native
case-folded search can also return a capitalized abbreviation or proper-name
article that the original lowercase query omits. This is not full compatibility
with the original reference DLL's search. Definitions remain plain text with
paragraph breaks; phonetic/font escapes are shown in brackets and unsupported
legacy characters as question marks. Original typography, pronunciation glyphs
and rich layout remain open. Membership and personal dictionaries are separate.

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

The native reader validates all 94,693 articles and indexes 61,362 ASCII
headword keys. Fifteen positive reference queries yielded identical expanded
entry markup for the IDs returned by the original; one negative query matched
as absent. This corroborates stream decoding, not complete query or formatted
text parity. The earlier all-article plain-text fingerprint is
`96f6f36e9bd7da36d4174793c19127c3`. The current validation fingerprint also
covers the lookup indexes (normalized keys and ordered article IDs).

The current indexes add alternate forms, subentries and etymological roots:

| Field category | Distinct ASCII keys | Key-to-article links |
| --- | ---: | ---: |
| Main headwords | 61,362 | 66,514 |
| Alternate headwords | 8,065 | 8,217 |
| Subentries | 22,583 | 22,626 |
| Stored inflections | 32,547 | 33,824 |
| Root derivatives | 3,805 | 3,906 |

Independent parsing of all 94,693 article streams checks all 135,087 index
links. There are 116,891 distinct keys across the five categories. An external
round-trip check validates all 131,609 returned articles and their match labels.
The index checksum `b4b10ca6eeb8dee21dafb12123c00f33` also matches an
independent standard MD5 calculation. The combined article/index fingerprint
is `a38393af143f77a0b9dbb2e8bcb7eca6`.
The external reference sample now contains 35 queries: 33 match the original's
complete ordered article-ID lists, including the formerly incomplete
six-article variant/root result. Two retain the documented case-filtering
difference: the native result also includes a capitalized article. This
corroborates the supported fields, not exhaustive original search compatibility.
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
and reset after failure. Scripted frontend checks browse variant, subentry and
root results, then close/reopen and replay while preserving the final score.

The 44 applicable English Boggle checks pass, including a fresh imported copy.
The all-language dictionary and complete resource survey checks were excluded
because this validation workspace holds the restored Classic dependencies only.
Six external SDL software-rendered runs with the installed resources cover
variant, subentry and root results in both Original and Master: opening,
scrolling, focus loss/return, closing/reopening, replay and unchanged scores. Reader, viewer, frontend and import
suites also pass ASan/UBSan (LeakSanitizer disabled for the execution tracer).
All 66 restored original files retain their manifest sizes and SHA-256 hashes.
The owner confirmed the definition viewer and stored-form extension work on
Windows. The alternate-headword, subentry and root extension still needs its
Windows test.
