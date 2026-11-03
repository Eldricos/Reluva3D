# Palettes and dice-set resources

These readers were independently written from the owner's files. Source
identities are in the [install manifest](../../../plans/boggle-1997/install-manifest.tsv),
with their CD equivalents in the companion manifest. The
[stage 1 report](../../../plans/boggle-1997/inspection.md) records the survey.
No retail RGB tables or dice-face lists are reproduced here or in tests.

## JASC palettes

All fourteen `Palettes/*.pal` files begin at offset zero with the JASC-PAL
identifier, version `0100`, and a decimal count of `256`. Then come 256 lines
of three decimal channel values in R/G/B order, each 0..255. CRLF and LF
lines are supported, with surrounding ASCII whitespace trimmed. Missing,
extra, nonnumeric or out-of-range components fail. No alternate version or
palette size is silently coerced. The input cap is 64 KiB.

The result is an array of 256 `Color` values. A failed read clears the
result. Test palettes are arithmetic gradients generated in code, independent
of the game's palettes. All fourteen supplied files pass.

## Dice sets (`Dicts/*.dst`)

The twelve observed files contain six comma-separated face fields per row.
Six `*Org.dst` files contain 16 rows; six `*Mas.dst` contain 25. They are
strong dice-set candidates based on the file contents and resource references;
loading them does not establish the original shuffle or board-generation rules.

The reader requires 16 or 25 rows and six nonempty fields per row. It trims
ASCII whitespace, bounds each face to eight bytes and the whole file to
64 KiB, and rejects ASCII punctuation/digits within faces. ASCII letters and
high bytes are preserved exactly. Multi-letter faces are retained as one
field. The Spanish files include byte `0xD1`; the reader deliberately does
not reinterpret it as UTF-8 or silently uppercase/transliterate letters.
Gameplay normalization and code-page mapping need separate evidence.

An artificial repeated die in the tests exercises row counts, multi-letter
faces and legacy bytes without incorporating any retail distribution. All
twelve supplied files pass (246 dice in total). Empty/user `.dic` files and
binary word dictionaries are a separate format, not accepted by this reader.
