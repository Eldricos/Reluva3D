# Wise installers

## What it is

The format of the installers Wise Solutions' tools made (Wise 5–7 era),
as far as Reluva reads them: enough to take a game's patch apart on any
system, without running it.

## What Reluva supports

`common/wise.cpp` (`readWiseInstaller`, `extractWiseStream`) walks the
files with `common/inflate.cpp`, a deflate decoder that reports how much
input each stream took. The engines say which files they want by their
signatures (size and MD5 of the first 5000 bytes, as detection's).
`reluva-tool import <source> <dest> -patch <file>` shows and applies it.

## The format

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

## Sources and checks

- **Checked on** Elite Force's 1.2 patch, `eliteforcepatch1_2.exe`
  (20,636,701 bytes): every file taken out of it is byte for byte the file
  a patched install has. The figures in the layout above are that
  installer's.
- **Confidence:** confirmed (worked out on the installer itself) unless marked.

## Used by

- **`idtech3`**: Elite Force's 1.2 patch
  ([patch.md](../engines/idtech3/games/stvef/formats/patch.md)).
