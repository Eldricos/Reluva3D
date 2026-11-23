# Installed word definitions

During a round, double-click a recorded word with the input empty. After a
round, select a results word and press **D**, or click **Definition**. Up/Down, Page Up/Down, Home/End and the mouse wheel scroll;
Left/Right or Previous/Next switch articles. Escape, Enter, D or Close returns
to play/results without changing scores, input, replay state or approved words.
The viewer holds gameplay time, including background opponents. Focus loss
adds an independent hold; focus-return and close batches cannot submit or end
a turn. Blue references open by click or Tab then Enter; Backspace/Back restores
the prior article and scroll position (up to 32 history pages). Enter follows
a keyboard-selected reference; otherwise it closes the viewer.

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

Article fields classify results for display: direct headwords, alternate
headwords, subentries, stored/other forms and roots. Ordering gives priority
to the case-folded **primary** printed headword, then retains authored index
order. An alternate WD inside a sense or inflection does not confer priority.
The same article appears once. The displayed match label identifies the parent
article independently of its rank.

The bounded rich-text decoder preserves Latin-1 as UTF-8, syllable dots,
pronunciation symbols and diacritics, primary/secondary stress weight,
headword/emphasis styles, raised/lowered text, sense labels and paragraph
breaks. See [rich-text format](formats/definition-text.md). Literal reference
lookup also exposes case-preserving names, phrases and accented spellings;
it is separate from lowercase ASCII game lookup. Indo-European root links
resolve against their own printed root-name catalog. Keyword fragments and
aliases without an article remain readable plain text rather than dead links.

An independently authored shared TrueType reader draws installed system fonts
(Times New Roman on Windows/macOS, DejaVu/Liberation Serif on Linux). It reads
Unicode cmap 4/12 and bounded simple/composite outlines; font hint programs
are never executed. Missing glyphs are filled from installed Arial/Segoe UI Symbol or DejaVu/
Liberation Sans fonts. DejaVu Serif plus Sans covers all 211 characters in
the supported archive. No fonts are bundled or copied from the game. An ASCII
fallback keeps navigation usable when no supported system font is available;
Unicode typography requires one of those fonts. The layout is native; exact
1997 font metrics, line breaks and dialog skins belong to the appearance task.
Definition membership, personal dictionaries and scoring remain independent.

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
The complete set of 14,597 lowercase multi-article keys was compared with an
isolated, read-only original reference-library oracle. All **14,596 successful
queries** match the complete ordered ID lists. One two-letter abbreviation
returns original error code 4, confirmed again in a fresh context; the native
reader resolves its valid authored links. This is below either Classic word
minimum and is intentionally not turned into a native lookup failure. No
retail spelling exceptions, original instructions or generated answer tables
are embedded in the runtime. The earlier 122 positive/negative probes remain
additional evidence; this comparison establishes exhaustive ordering for the
supported archive's successful multi-article ASCII queries, not every API or
language supported by the reference DLL.

All 94,693 articles pass rich-text decoding with **zero unsupported symbol
names**. Metadata fingerprints cover text, styles and reference targets. The
combined lookup checksum is `315f1cd7c1151d3b90a007318dd9b008`; the
article-plus-index validation fingerprint is
`85a2fefbbe30a2e5b4b1976a926a6973`.

`reluva-tool boggle definition-check <folder> [ASCII headword]` validates the
full archive or a selected lookup, printing counts and checksums only. Optional
`boggle.gamedata.definitions` and fresh-import checks preserve metadata only.
The rich-text fingerprint has its own
game-data record. File selection and save formats are unchanged.

Synthetic tests cover both readers, ranking, duplicate suppression, input case,
implicit forms, root spelling, authoritative no-hit behavior, signed deltas,
case flags, partial block boundaries, sentinels, malformed tables/records,
reset after failure and scrolling/focus/close guards. Scripted game tests enter
an authored form, browse results, close/reopen and replay without changing score.
The milestone adds synthetic Unicode/composite-font fixtures, pronunciation and
field-style checks, literal/root references, click/keyboard/back navigation,
confirmed transactional Clear, immediate refusal/retry and two-player Chess
handoff checks. Traditional/Chess scripts open definitions across 90 seconds
and focus loss, then verify that another word still counts. The original
Game Over definition edit control has ES_READONLY (resource 15700/15701), so
an arbitrary-word search box is not a missing original feature.

All original resources, oracle programs/output and visual captures stay outside
Git. Windows owner validation of this completed milestone is still to follow;
previous owner confirmations apply to the earlier features.

Final completion validation: 46 applicable CTest checks pass, including all
Boggle synthetic suites, 17 supplied-data checks, Unicode fonts and test
coverage. The two full resource/all-language game-data surveys require files
outside the restored 66-file Classic set and were excluded explicitly. The
Boggle/Unicode suite and complete definition corpus pass ASan/UBSan; the final
viewer changes are covered by an additional focused sanitizer run.
SDL software runs for both Original and Master verify in-round/results viewer
parity, scroll/focus/close/replay flow and unchanged saved scores. All 66 original
files still match the acquisition manifest. No original file was modified.

The final reference scan exposes 20,483 distinct literal targets and 499 root
targets, all resolvable; non-target keyword labels keep their original text.
The rich text/style/reference fingerprint is
`a7fdb233413cd9b0245316f77ed96348`.
