# Definition word index

Independent narrow support for the word-index component in the supplied
`Dicts/R016E613.DAT`. The engine reads data only; it does not execute the
original DLL, suffix records or grammar programs. Article decoding is described
in [definitions](../definitions.md).

## Container and sections

The container's big-endian count at 0x316 is three, with a table pointer at
0x318. Three 12-byte rows at 0x32a contain a component type, header offset and
stream offset. Types are 0x00010001 (entries), 0x00020002 (word index) and
0x00020003 (Boolean search). The reader checks the entry stream against the
entry reader, and bounds the index before the Boolean component.

For this release the word-index header is at 0x963e00 and its stream at
0x970873. The header contains version 0x00020002, marker `Index file`, block
count at +0x1e, block size at +0x8c, directory key width at +0x8e, maximum word
length at +0x92 and symbol width at +0x94. Measured values are 1,720 blocks,
512 bytes, four directory bytes, 62 word bytes and 10 symbol bits.

Ten 16-byte big-endian descriptors begin at +0xaa. Each holds a tag, relative
offset, flags, stride, count and byte length. All sections must be in bounds,
disjoint and precede the declared stream; their maximum end equals that stream.
The required tags are 200, 210, 220, 230, 240, 250, 260, 270, 290 and 300.
Only the following sections are interpreted by this native reader:

| Tag | Purpose |
| --- | --- |
| 200 | Literal byte pool |
| 210 | Little-endian 16-bit literal offsets, one per symbol |
| 220 | Byte lengths, one per symbol |
| 230 | Four-byte block directory prefixes |

There are 1,024 symbols. Symbol zero expands to empty; other literals are
nonempty. Every offset/length pair is validated. Other sections are bounded
but never treated as executable programs.

## Blocks and records

Each block independently packs MSB-first 10-bit symbols, ignoring incomplete
bits at its physical end. Literal expansion reconstructs front-compressed
records. A record starts with a byte whose low six bits remove that many bytes
from the previous key's end. Its high two bits carry case information. A suffix
follows, terminated by byte 7, then bytes 0 and 0x10 and a signed big-endian
24-bit article-ID delta. The previous key and ID reset at each block.

The game-query index keeps records with zero case bits and lowercase ASCII
letter keys of at most 64 bytes. IDs must be within the validated article range.
Duplicate key/ID pairs are suppressed. Non-ASCII, punctuation and multiword
records are structurally validated and exposed separately for literal
reference navigation, without changing ASCII game queries. Case flags restore
initial-cap (0x80), all-cap (0xC0), literal mixed-case (0x40) or lowercase (0)
spelling; legacy characters convert to UTF-8.

A block can end with an incomplete lookahead record. Its decoded bytes must
match the prefix of the next block's complete first record, including case,
backtrack, spelling and delta. Directory prefixes match the first key's first
four ASCII-case-folded bytes, padded with spaces; the first directory key is
four spaces. The final data block permits zero padding. The final physical
block is a sentinel: zero, the last key, zero, its absolute big-endian 32-bit
article ID, and zero padding. Its directory prefix must also match.

## Bounds and tests

The implementation limits archives to 32 MiB, blocks to 4,096, block sizes to
64–4,096 bytes, symbols to 2–12 bits, words to 128 bytes, a literal cache to
1 MiB, expanded blocks to 64 KiB each and 64 MiB total, records to 500,000,
game-query links to 400,000 and distinct game articles per key to 64. The
separate literal-reference index is bounded by the same 500,000 source records
and 4,096 distinct articles per spelling; the viewer retains its 64-article
limit and reports an oversized reference instead of rendering it. Unsupported
headers, metadata, impossible backtracks, invalid IDs or boundary/sentinel
mismatches clear the result and return an error.

Synthetic tests independently build literal tables, two data blocks, a partial
lookahead and a sentinel. Invented words exercise negative deltas, duplicate
keys, upper-case exclusions, implicit forms and authoritative missing keys.
Malformed headers, ranges, codebooks, records, boundaries and sentinels fail
without retaining prior state. Retail block contents and decoded word lists
remain outside Git.
