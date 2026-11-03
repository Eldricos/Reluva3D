# Boggle word dictionaries

This specification comes from the owner's retail files and executable
analysis for interoperability. No original instructions are used by the
engine. Source EXE SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Dictionary SHA-256 identities are in the
[install manifest](../../../plans/boggle-1997/install-manifest.tsv).

## Confirmed serialized layout

The binary loader at EXE VA `0x433a60` opens a file and invokes the recursive
node reader at `0x432990`. There is no file-level magic, count or pointer table.
Nodes are serialized in depth-first order, starting at byte zero:

1. A little-endian 32-bit terminal-letter mask, with bit 31 indicating an
   internal node. Low bits 0..25 identify A..Z; bit 26 identifies byte `0xD1`.
2. A little-endian 32-bit continuation mask.
3. If the continuation mask is zero, there is no payload.
4. Otherwise, if bit 31 of the first mask is set, one child node follows for
   each set continuation bit, in increasing bit order. Each child adds its
   associated letter to the current prefix.
5. Otherwise, a little-endian 32-bit byte length follows, then a suffix-list
   payload of exactly that length. It contains suffixes of at least two bytes,
   each NUL-terminated, followed by an additional NUL terminator.

Terminal-mask letters emit prefix + letter. A leaf suffix emits prefix +
suffix. Suffix lists are not universally sorted or unique: the AI, Dutch and
German files include duplicate entries. A reader must not reject these as
corruption or depend on their ordering. Bits 27..30 of the first mask and
bits 27..31 of the continuation mask were zero in all observed nodes.

This layout consumed every byte of all eight nonempty `.dic` files. Six
`*User.dic` files are empty and belong to a separate text loading path
(`0x432bd0`), not this binary format. The INSO definition `.DAT` is unrelated
and is not decoded here.

## Lookup and encoding evidence

Exact lookup is at `0x432770` (wrapper `0x432e30`); prefix lookup is at
`0x432880` (wrapper `0x432e40`). Exact lookup checks a terminal bit when one
input byte remains, descends internal nodes by continuation-bit rank, and
compares a remaining suffix against complete leaf strings. Prefix lookup
permits a shorter match against a leaf suffix. These are evidence of behavior,
not an implementation template: the native reader can decode to a sorted,
deduplicated collection and perform independent binary-search lookup.

`0x467360` uppercases input and explicitly maps byte `0xF1` to `0xD1`.
Nevertheless, Spanish leaf suffixes contain `0xF1`, while tree edge/terminal
bit 26 emits `0xD1`. Preserve serialized bytes during decoding; do not claim
that global case conversion reproduces all Spanish lookup behavior. The
initial Classic slice will use U.S. English. Multilingual normalization and
any original encoding inconsistency require separate validation.

Raw disassembly, decoded words and analysis harnesses remain outside Git.
Only original parser code, synthetic fixtures and metadata records may be
committed. Original-routine comparisons and native-reader survey results follow.

## Native-reader verification

The native reader decodes to a sorted, deduplicated word collection. Its
binary search and prefix search operate on preserved serialized byte strings;
they do not execute the EXE or link INSO libraries. Limits are 32 MiB input,
one million nodes, two million emitted entries, 64 bytes per word and 64 MiB
aggregate word bytes. Invalid masks, lengths, terminators, suffix characters,
truncations, excess depth and trailing data clear the result and fail.

| File | Serialized entries | Unique decoded words |
|---|---:|---:|
| AI | 18,651 | 18,649 |
| British | 112,427 | 112,427 |
| Dutch | 198,753 | 198,717 |
| English | 112,427 | 112,427 |
| French | 115,730 | 115,730 |
| German | 92,885 | 92,423 |
| Spanish | 222,290 | 222,290 |
| UK | 107,349 | 107,349 |

An external research harness mapped the owner's EXE into Reluva's x86
interpreter, supplied bounded file-read/allocation callbacks, invoked the
original loader (`0x432990`) and compared the original exact lookup
(`0x432770`) with the independent reader. It queried every unique decoded
word and one deterministic first-letter mutation per word. All dictionaries
except Spanish had zero mismatches. Spanish had two mutation queries accepted
by the original but absent from the decoded set, involving its legacy-byte
aliases. Every decoded Spanish word was accepted. These aliases are not
silently generalized or advertised as matching Spanish gameplay.

The harness and its original-code execution exist only in the external
analysis directory; none of that code or the retail executable is part of
the runtime path. Sampling proper prefixes produced no false negatives in
the original prefix routine. Native unit tests construct an invented tree
with branches, unsorted suffixes, duplicates and legacy bytes. No retail
entries or suffix blocks are embedded in tests or this document.
