# Reading PE resources without executing code

`common/pe_resources.h` exposes an independent resource reader for PE32 and
PE32+ images. The initial use is Boggle's language DLLs. It reads bytes only;
it does not load DLLs, run entry points, resolve imports or use the x86 host.
The reader is separate from `parsePe`, so reading UI resources does not walk
unrelated import/export/relocation tables.

The implementation was written from direct header/directory inspection of
the owner's files, following the PE resource layout. The Boggle
[inspection report](../plans/boggle-1997/inspection.md) and manifests identify
these inputs. No proprietary binary instructions or external implementation
were translated into this code. Synthetic fixtures assemble their own PE
headers and resource bytes in `tests/support/pe_resources_fixture.h`.

## API and layout

`readPeResources` validates the DOS/PE/optional headers, directory table and
raw section bounds, then walks type -> name -> language -> data-entry.
Directory offsets are relative to the resource directory base; payload
addresses are RVAs mapped through file-backed sections. A mapped range must
fit wholly in one unambiguous raw section; virtual-only data is not read.
Both integer keys and length-prefixed UTF-16 names are preserved. Language
IDs are explicit 16-bit values. `findPeResource` requires an exact type,
name and language; it does not choose an arbitrary fallback.

`readPeStrings` decodes RT_STRING blocks (numeric type 6), each containing
16 length-prefixed UTF-16LE strings. The ID is
`16 * (block ID - 1) + slot`. Nonempty strings are returned as UTF-8, sorted
by ID and language. Valid surrogate pairs are combined; unpaired surrogates
fail. Up to three trailing zero alignment bytes are allowed. Duplicate
ID/language pairs and invalid block IDs fail.

Resources retain code-page metadata and owned payload bytes for later dialog,
menu or bitmap readers. Dialog templates and other resource types are not
interpreted yet. No extracted payloads belong in repository fixtures.

## Bounds and failures

Inputs are capped at 128 MiB; directory work at 65,536 entries; decoded-name
work and aggregate owned payload/name bytes at 64 MiB each. The directory
shape fixes recursion to three levels. Invalid offsets, cycles/depth,
truncated names/entries/payloads, duplicate keys, invalid UTF-16 names,
ambiguous section mappings and excessive counts fail. String decoding has a
64 MiB aggregate UTF-8 cap. Each public reader clears its output on failure.

`common.unit.pe_resources` tests PE32/PE32+, named keys, language separation,
supplementary characters, all file-prefix truncations of a synthetic image,
malformed directory/data ranges and invalid string blocks. The local Boggle
resource survey also reads all five retail resource DLLs (1,001 resource
leaves and 1,976 nonempty strings), while committing only counts/checksums.
