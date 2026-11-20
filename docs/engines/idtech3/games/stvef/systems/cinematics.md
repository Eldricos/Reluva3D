# Elite Force: cinematics

> **Status:** done · **Code:** `src/engines/idtech3/client/sp_play_page.cpp` · **Checked against:** the code only (`stvoy.exe`) · **Confidence:** from the code unless marked

## Overview

Elite Force's cinematics are Bink movies (`video/<name>.bik`), decoded by
Reluva's shared decoder ([bink.md](../../../../../common/video/bink.md)).

## How it works

- **`cinematic <name>`** (`stvoy.exe` 0x404840) ends the game and plays
  `video/<name>.bik` full screen. When it ends, the engine runs the
  `nextmap` cvar's command and clears it.
- **`ingamecinematic <name>`** plays one over a running game. The game's
  scripts send it as `inGameCinematic <name>`.
- **At start-up** the engine plays `cinematic eflogo`.
- **Skipping.** A typed key (ASCII, Esc included) or the first mouse
  button skips a cinematic.
- **Variants the engine also looks for:** `_lo` versions on slow machines,
  and a German check.

## In Reluva

`SpPlayPage` (`src/engines/idtech3/client/sp_play_page.cpp`) plays
cinematics (`startCinematic`, `endCinematic`). It plays the plain name,
not the `_lo` or German variants (also in
[KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
`reluva-tool idtech3 bink-decode` decodes a movie's frames and sound, to
compare with another decoder.

## Sources and checks

- **Where the facts come from:** `stvoy.exe` (the `cinematic` command at 0x404840).
