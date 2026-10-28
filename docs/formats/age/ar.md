# .ar archives

The Angel Game Engine keeps a game's data in `.ar` archives. Midtown
Madness 2 ships four in its install folder (and on the CD's `GAME` folder,
the same files with upper-case names): `mm2core.ar`, `mm2tex.ar`,
`mm2aud.ar` and `mm2audex.ar`. Read by `src/engines/age/ar_archive.cpp`.

## How this was worked out

From the retail archives alone. The first 16 bytes read as `DAVE` and
three plausible counts; the 16-byte records at 2048 had a first field
that pointed at zero-terminated names (`anim/CVS/Entries`...) once the
names were found to start right after the directory, and offsets that
landed on data. Files whose two sizes differed inflated as raw deflate to
exactly the larger one. Confirmed by unpacking all four archives (13,288
files) with every size matching, once with a throwaway script and once
with `reluva-tool ar-extract`, which gave the same bytes.

## Layout

All numbers are little-endian u32.

| Offset | Contents |
|---|---|
| 0 | `DAVE` |
| 4 | number of entries |
| 8 | size of the directory |
| 12 | size of the names |
| 16 | zeros up to 2048 |
| 2048 | the directory: one 16-byte entry per file |
| 2048 + directory size | the names |
| after the names | the files' data |

An entry:

| Offset | Contents |
|---|---|
| 0 | where the name starts, from the start of the names |
| 4 | where the data starts, from the start of the archive |
| 8 | the file's size |
| 12 | its stored size |

- **Names** are full paths with `/` separators, zero-terminated, in the
  developers' mixed case (`tune/VPCOOP.info`). Folders have entries of
  their own, ending in `/` with both sizes 0; Reluva leaves them out of
  the file list. In every retail archive the entries are sorted by name
  ignoring case, which suggests the original looks names up by a binary
  search; Reluva uses a hash table and doesn't rely on the order.
- **Data** is raw deflate (RFC 1951, no zlib header) when the stored size
  differs from the size, and stored as is when they're equal. A file
  deflating to exactly its own size would be ambiguous; none does in the
  retail archives, and the original presumably stores those as is.
- **Padding**: the directory and the names are each padded to a multiple
  of 2048 bytes (their sizes in the header include it), and every file
  starts on a 2048-byte boundary, the CD's sector size.

| Archive | Files | Deflated | Size unpacked |
|---|---|---|---|
| mm2core.ar | 5,667 (and 13 folders) | 5,496 | 48 MB |
| mm2tex.ar | 4,365 | 4,364 | 180 MB |
| mm2aud.ar | 1,843 (and 25 folders) | 555 | 117 MB |
| mm2audex.ar | 1,413 (and 12 folders) | 0 | 143 MB |

The archives also carry what the developers had in their working folders:
CVS bookkeeping (`CVS/Entries`), backups (`.bak`, `Copy of ...`), batch
files and logs.

## Not known yet

- **Other headers.** Only `DAVE` has been seen; other AGE games or
  versions may differ. Anything else is refused.
- **Search order.** No two retail archives hold the same name, so their
  order doesn't matter for the game itself. Reluva mounts every `.ar` in
  the game folder, later names (in lower case) over earlier ones, with
  loose files in the folder over all of them. Mods are said to come as
  extra `.ar` files, so the original reads more than its own four, but
  which wins when two hold the same name hasn't been checked against it.
