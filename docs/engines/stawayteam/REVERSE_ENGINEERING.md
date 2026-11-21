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
- **Pictures:** the frame loader found through its `.frm16` string, for
  the header; the blocks' sizes added up to each file's; the run-length
  codes worked out from rows by hand and tried on every row of every
  frame with a first decoder written apart from Reluva (in Python, not
  kept), then Reluva's checked against it pixel for pixel and by looking
  at pictures it wrote out.
- **Objects in binary:** found through the constant 26.0 every such file
  starts with. The classes' registrations then read out of the whole
  disassembly by a script, each pattern of the compiler's code looked at
  by hand and the script taught it, until every class's version, worked
  out as the game works it out, matched the files' and every binary file
  decoded to its last byte ([research/binary-objects.md](research/binary-objects.md)).
  The research scripts are in `tools/stawayteam-re/`.

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
| `reluva-tool stawayteam frame-check` | Decodes every `.frm16` picture in `DATA.dat` |
| `reluva-tool stawayteam frame-png` | Writes `.frm16` pictures out as PNG files |

Research scripts, not part of Reluva, in `tools/stawayteam-re/`
(its README says how to run them):

| Script | What it does |
|---|---|
| `stat_classes.py` | Reads every class and its properties out of `Star Trek.exe`'s disassembly: [research/classes.txt](research/classes.txt) |
| `stat_objects.py` | Decodes every sequence, model and waypoint map in `DATA.dat` with that schema, to check it |

## Working files not included

- **`DATA.dat`'s files, written out:** `reluva-tool stawayteam
  data-extract <DATA.dat> <folder>` from your own copy.
- **Its pictures as PNG files:** `reluva-tool stawayteam frame-png
  <DATA.dat> <folder>`.
- **The disassembly of `Star Trek.exe`** the class scripts read:
  `objdump -d -M intel --no-show-raw-insn "Star Trek.exe"`.
