# Installed word definitions

After a round, select a word in the results list and press **D**, or click
**Definition**. Up/Down, Page Up/Down, Home/End and the mouse wheel scroll;
Left/Right or Previous/Next switch articles. Escape, Enter, D or Close returns
to results without changing scores, replay state or approved words.

The installed `Dicts/R016E613.DAT` is loaded on first use. Older imports remain
playable: re-import into a new external library folder, or copy this archive
from your own installation's `Dicts` folder into the imported `Dicts` folder.
Missing, corrupt and unmatched entries have a recoverable viewer message.
No original reference DLL is imported or executed by the engine.

## Supported behavior and limits

The archive's authored word index supplies matching article IDs. This includes
implicit inflections and root links that cannot be reconstructed from the
printed article fields alone. No English suffix rules, stemming guesses or
retail word exceptions are built into the engine. An invalid declared index
fails rather than silently reverting to article-derived matches.

Input case follows Boggle's wrapper: a requested word is treated as lowercase.
Only the index's lowercase ASCII letter keys are exposed. Capitalized-only
names and abbreviations therefore do not join lowercase results. A lowercase
indexed form can still lead to a capitalized article or a differently printed
root derivative. Direct article-ID access retains all validated articles.

Article fields classify results for display and ordering: direct headwords,
alternate headwords, subentries, stored/other word forms, then roots. The same
article appears once. Alternate spellings inside an inflection group retain
inflection rank. The viewer names the parent article and labels newly indexed
forms as **Word form of**. Word-index order is retained within each rank.

Definitions remain plain text with paragraph breaks. Phonetic/font escapes
are shown in brackets and unsupported legacy characters as question marks.
Original typography, pronunciation glyphs, rich layout and exhaustive search
ranking compatibility remain open. Membership, personal dictionaries and
scoring are independent of definition lookup.

## Independent readers

The supported archive is 13,263,360 bytes, SHA-256
`49789292122be628e46bad04cec4142c23e6513ac19123c76110babf108b54d1`.
The entry component begins at 0x400. Seven big-endian section descriptors name
a little-endian page directory, two symbol tables, a literal pool and grammar
sections. MSB-first 13-bit symbols expand to literals or pairs; a four-FE-byte
literal ends an article. The 64-entry directory points to the preceding
terminator at each page boundary. All articles are validated before indexing.
The native reader does not execute the grammar sections.

The container directory also identifies a separate word-index component.
Its bounded reader validates literal codebooks, front-compressed keys, case
flags, signed article-ID deltas, block lookahead, directory prefixes and the
terminal sentinel. See [word-index format](formats/definition-index.md).
Entry-only synthetic fixtures retain explicit-field lookup; the supported
retail archive always uses its declared authored index.

Both readers bound sections, offsets, expansions, word lengths, total records
and per-key matches. Failed reads clear prior state. Invented fixtures assemble
entries and index blocks independently; no retail definitions, codebooks,
word lists, decoded entries or original instructions are committed.

## Evidence and validation

Read-only original analysis links initialization at 0x432f10 and lookup at
0x433360 to the reference DLL APIs. An external interpreter opens the owner's
archive using read-only callbacks. The game's loop at 0x4333b8–0x4333ec calls
`MSVCRT.dll!tolower` through import 0x495270. Entry decoding at 0x10048e80 and
paging at 0x10049880 corroborate the entry stream; query traces establish
word-index block reads. Original modules and all raw research remain outside
Git and are never runtime dependencies.

The reader validates all 94,693 articles and 297,971 word-index records. There
are 125,629 supported word keys and 142,676 unique word-to-article links.
Independent Python decoding reproduces every authored link. The word-index
checksum is `13470544fea1cfd3b241eb162e41c60a`. An external round-trip check
validates all 142,676 returned articles and their nonempty text/headings.
All 122 original-reference queries now match the complete ordered article-ID
lists, including both previously unresolved cases. The sample covers regular
and irregular forms, invalid forms, homographs, names and roots. This is broad
corroboration, not exhaustive equivalence of the original reference library.
The combined lookup checksum is `315f1cd7c1151d3b90a007318dd9b008`; the
article-plus-index validation fingerprint is
`85a2fefbbe30a2e5b4b1976a926a6973`.

`reluva-tool boggle definition-check <folder> [ASCII headword]` validates the
full archive or a selected lookup, printing counts and checksums only. Optional
`boggle.gamedata.definitions` and fresh-import checks preserve metadata only.
Only definition metadata changes in the import record; file selection, existing
play/audio/artwork checks and save formats are unchanged.

Synthetic tests cover both readers, ranking, duplicate suppression, input case,
implicit forms, root spelling, authoritative no-hit behavior, signed deltas,
case flags, partial block boundaries, sentinels, malformed tables/records,
reset after failure and scrolling/focus/close guards. Scripted game tests enter
an authored form, browse results, close/reopen and replay without changing score.
All 44 applicable Boggle checks pass, including fresh-import validation. The
full resource survey and all-language dictionary tests are excluded because
this workspace holds only the restored Classic dependencies. Reader, frontend
and import suites pass ASan/UBSan with LeakSanitizer disabled for the tracer.
External SDL runs in Original and Master enter an indexed form, inspect it,
scroll, lose/restore focus, close/reopen and replay with the same final score.
The new reader and lookup still require the owner's Windows validation.
