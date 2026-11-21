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
  `objdump -p`, and its strings. Nothing of it has been run.
- **The readme** that comes with the game (`readme.rtf`): what the game
  offers, such as the network game being two players, cooperative, on a
  LAN only. The manual (`Extras/Help/Manuals/manual.pdf`) is to be read
  next.

## Methods

- **Detection:** each file's signature (the MD5 of its first 5,000
  bytes, and its size) made with `reluva --hash`, and checked with
  `reluva-tool detect` on the install.
- **`DATA.dat`:** read by walking its local file headers, since its end
  of central directory record isn't signed as ZIP's is.

## The tools

`reluva-tool help stawayteam` lists the engine's commands
(`src/engines/stawayteam/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool stawayteam files` | Lists the game's folder |

## Working files not included

None yet.
