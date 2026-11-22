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

Lookup matches case-folded ASCII headwords, ignoring internal dictionary
syllable dots. Multiple matching homographs can be browsed. This is exact
headword lookup: the original DLL's morphology, stemming and query ranking are
not reproduced. Some inflections have their own cross-reference articles, while
others may have no match. Definitions are plain text with paragraph breaks;
phonetic/font escapes are shown in brackets and unsupported legacy characters
as question marks. Original typography, pronunciation glyphs and rich layout
remain open comparison work. The membership dictionary and personal word editor
are independent of this archive.

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
headers fail closed and clear previous state. Synthetic fixtures independently
construct tiny archives with invented articles and malformed variants; no
retail definitions, codebooks or decoded entries are committed.

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
text parity. The native all-article plain-text fingerprint is
`96f6f36e9bd7da36d4174793c19127c3`.

`reluva-tool boggle definition-check <folder> [ASCII headword]` checks the full
archive or a selected lookup, printing only counts and checksums. Optional
`boggle.gamedata.definitions` and fresh-import checks preserve metadata only.
Synthetic tests cover decoding, rejection/reset, scrolling, match navigation,
focus/close event guards, unchanged scores and missing/corrupt archive recovery.


The 44 applicable English Boggle checks pass, including a fresh imported copy.
The all-language dictionary and complete resource survey checks were excluded
because this validation workspace holds the restored Classic dependencies only.
External SDL software-rendered runs with the installed resources cover Original
and Master definition opening, scrolling, focus loss/return, closing/reopening,
replay and unchanged finalized scores. Reader, viewer, frontend and import
suites also pass ASan/UBSan (LeakSanitizer disabled for the execution tracer).
All 66 restored original files retain their manifest sizes and SHA-256 hashes.
Windows testing of this new viewer remains pending.
