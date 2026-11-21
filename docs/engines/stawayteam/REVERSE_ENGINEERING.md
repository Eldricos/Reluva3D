# Star Trek: Away Team: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** a US English retail install and its CD,
  November 2026: file listings, sizes and MD5s, and the start of files of
  each kind in `DATA.dat`, dumped as hex or text
  ([research/file-survey.md](research/file-survey.md)).
- **The executable:** `Star Trek.exe` (2,437,120 bytes, MD5
  `cd908f2d79a8a331045fa1d80414ef0a`): its headers and imports read with
  `objdump -p`, its strings, and its code disassembled with `objdump -d`
  where a constant leads to it (the archive's signatures). Nothing of it
  has been run.
- **The readme** that comes with the game (`readme.rtf`): what the game
  offers, such as the network game being two players, cooperative, on a
  LAN only. The manual (`Extras/Help/Manuals/manual.pdf`) is to be read
  next.

## Methods

- **Detection:** each file's signature (the MD5 of its first 5,000
  bytes, and its size) made with `reluva --hash`, and checked with
  `reluva-tool detect` on the install.
- **`DATA.dat`:** first read by walking its local file headers, since its
  end of central directory record isn't signed as ZIP's is; then the
  executable searched for both signatures as 32-bit constants, which led
  to its reader's check; then every file read with Reluva's reader.
- **Objects in text and scripts:** every text file in `DATA.dat`
  surveyed with scripts (the kinds of line, arrays and their counts,
  keys more than once, spaces at lines' ends), then the executable's
  reader found through its `{`, `}` and `=` strings and disassembled;
  Reluva's readers checked by writing every object back and comparing it
  with its file.

## The tools

`reluva-tool help stawayteam` lists the engine's commands
(`src/engines/stawayteam/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool stawayteam files` | Lists the game's folder |
| `reluva-tool stawayteam data-list` | Lists `DATA.dat`'s files and their sizes |
| `reluva-tool stawayteam data-extract` | Writes out `DATA.dat`'s files, checking their CRC-32s |
| `reluva-tool stawayteam data-check` | Reads every object and script in `DATA.dat`, writing each object back to compare |
| `reluva-tool stawayteam text-show` | Shows one object's fields or one script's entries as Reluva reads them |

## Working files not included

- **`DATA.dat`'s files, written out:** `reluva-tool stawayteam
  data-extract <DATA.dat> <folder>` from your own copy.
