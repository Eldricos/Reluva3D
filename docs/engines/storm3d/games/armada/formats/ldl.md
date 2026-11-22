# Star Trek: Armada: installer scripts (`.ldl`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/ldl.cpp` · **Checked on:** the English retail 1.0 install's six `.ldl` files · **Confidence:** confirmed unless marked

## Overview

Six encoded text files in the install's folder, also on the CD:

| File | Size | What it is, by its own first lines |
|---|---|---|
| `string.ldl` | 126,794 | The installer's script for every language: screens, buttons, fonts, file copies, registry entries, shortcuts, system requirements |
| `string_e.ldl` | 31,561 | Its English texts |
| `string_fi.ldl`, `string_gi.ldl`, `default.ldl` | 1,501, 1,500, 1,368 | What a French, a German or another release adds: each imports `string.ldl` and `string_e.ldl` (`import "string.ldl"`), then has empty blocks |
| `language.ldl` | 10,031 | Which of the files above to use for Windows' language |

They're scripts for the CD's `setup.exe`, as `string_e.ldl` says ("this
ldl file is a script interpreted by setup.exe"). The game doesn't need
them to run; they're read here to know the installer's choices, for
importing from the CD.

## Layout

### Encoding

Every byte is XORed with a 400-byte key, the key's byte at the file
offset modulo 400. The key is the same for all six files. Every file
starts with `# Star Trek`, so they all start with the same 11 encoded
bytes.

The key is in both programs' data: in the CD's `setup/setup.exe`
(305,664 bytes) at file offset 192,064, and in `Armada.exe` at `0x604548`
(in `.data`). Its MD5 is `a7a514b8967277ed3bbe9be5eb3cda6b`.

### The scripts

Decoded, a file is text with `#` comments and nested blocks in braces.
`string.ldl`'s first lines list the kinds of block, each marked by a tag
in a comment (`#ABLOCK` actions, `#SBLOCK` screens, `#FCBLOCK` file
copies, `#RBLOCK` registry entries and 15 others). The language itself
isn't read yet.

## In Reluva

`readLdlKey()` (`src/engines/storm3d/formats/ldl.h`) reads the key from
the player's `Armada.exe` and checks it by its MD5; the key isn't in
Reluva. Only the English 1.0 executable's place for it is known.
`decodeLdl()` decodes a file with it.

- `reluva-tool storm3d ldl <game folder> <file.ldl> [output file]`: a
  file decoded, printed or written.

## Sources and checks

- **Where the facts come from:** the six files. The key's length was
  found by how often bytes repeat at each period: at 400 (and its
  multiples) the columns look like text, at others like noise. Each of
  its bytes was then the one that made all six files' columns most like
  English text. Found the same in both programs, byte for byte.
- **How it was checked:** all six files decode to text with no control
  characters, starting with their own names; the unit tests
  (`storm3d.unit.ldl`) check the decoding with a key made in the test.

## Open questions

- What `Armada.exe` uses the key for: it holds the same table, but no
  `.ldl` file's name.
- The scripts' language, for importing a game from the CD.
