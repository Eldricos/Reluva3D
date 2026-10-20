# id Tech 3: `.pk3` archives and the search path

Status: **implemented**; checked on archives built by Python's `zipfile`
and by the tests. Not yet checked against the retail Elite Force paks.
Readers: `src/common/zip.cpp` (the archive), `src/engines/idtech3/filesystem.cpp`
(the search order).

Sources: PKWARE's published `APPNOTE.TXT` for the zip format; the search
order from the Quake III Arena source id Software released under the GPL
(`code/qcommon/files.c`, `FS_AddGameDirectory`), read for behaviour, not
copied.

## Format

A `.pk3` is an ordinary zip file. Entries are stored (method 0) or
deflated (method 8). Reluva reads the central directory at the end of the
file, then each entry's local header for where its data starts (the local
name and extra field lengths can differ from the central directory's).
Every read is checked against the entry's CRC-32.

Not supported, and not expected in game data: encryption, Zip64, archives
split over several files.

## Search order

A game folder (`baseEF` for Elite Force) is searched in this order, first
match wins:

1. loose files in the folder and its subfolders;
2. the folder's `.pk3` files, the last name first, comparing names in
   upper case (so `_` sorts after the letters): `pak2.pk3` before
   `pak1.pk3` before `pak0.pk3`, and `pak_x.pk3` before `paka.pk3`. That's
   how patches replace files: they add a later pak.

A mod's folder goes on top of the base game's, in the same order inside it
(`mountGameDir`'s `layer`). File names are matched ignoring case, with `\`
and `/` treated alike.

## Elite Force

The retail game keeps its data in `baseEF/pak0.pk3`; patches and the
Expansion Pack add more paks. **To check with an owned copy:** the list of
paks per release, and their sizes and MD5s for the detection table
(`reluva --hash baseEF/pak0.pk3`).
