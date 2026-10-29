# Wise installers

The format of the installers Wise Solutions' tools made (Wise 5–7 era),
as far as Reluva reads them: enough to take a game's patch apart on any
system, without running it. Checked on Elite Force's 1.2 patch,
`eliteforcepatch1_2.exe` (20,636,701 bytes).

## Layout

1. **A small Windows program** (`STUB32.EXE` in its export table, 0x3a00
   bytes for Elite Force's). Its strings name `WiseMain` and
   `Wise0132.dll`, which it unpacks and calls. Its sections end where
   the installer's data starts (here 0x3a00).
2. **A header:** a few binary fields, then NUL-terminated strings: the
   dialog font (`MS Sans Serif`), the title ("Star Trek: Voyager - Elite
   Force v1.2 update Installation") and "Initializing Wise Installation
   Wizard...". Reluva doesn't read it; it finds the first file as the
   first offset after the program where a deflate stream starts whose
   CRC follows it (here 0x3acd).
3. **The files, end to end:** each a raw deflate stream (RFC 1951, no
   zlib header), then the CRC-32 of the file (as zip's, little endian).
   Nothing marks where a stream ends but the stream itself, so each must
   be decoded to find the next. No names: they're in the installer's
   script, one of the files (the second here, 14,963 bytes), which
   Reluva doesn't read; it tells files by their contents.

Elite Force's 1.2 patch holds 33 files, the last ending at the end of the
installer. Among them, whole (not as differences from the old files):

| # | Offset | Packed | Size | File |
|---|---|---|---|---|
| 2 | 0x601b | 86,709 | 160,768 | `Wise0132.dll` (the installer's own) |
| 21 | 0x49157 | 428,322 | 925,696 | `stvoyHM.exe` 1.2 |
| 22 | 0xb1a7d | 338,764 | 720,963 | `stvoy.exe` 1.1 |
| 24 | 0x122a76 | 442,801 | 974,924 | `efgamex86.dll` 1.1 |
| 25 | 0x18ec2b | 137,675 | 376,904 | `efuix86.dll` 1.1 |
| 26 | 0x1b05fa | 6,427,478 | 6,492,912 | `baseEF/pak1.pk3` |
| 27 | 0x7d1954 | 12,329,744 | 12,408,486 | `baseEF/pak2.pk3` |

Each is byte for byte the file a patched install has. The rest are the
installer's screens, readme (RTF), text and settings (`[ActiveNet]`...).

## Reluva

`common/wise.cpp` (`readWiseInstaller`, `extractWiseStream`) walks the
files with `common/inflate.cpp`, a deflate decoder that reports how much
input each stream took. The engines say which files they want by their
signatures (size and MD5 of the first 5000 bytes, as detection's); Elite
Force's are `pak1.pk3`, `pak2.pk3` and the two 1.1 DLLs (`games.cpp`,
`patchFiles`). `reluva-tool import <source> <dest> -patch <file>` shows
and applies it.
