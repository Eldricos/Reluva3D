# Elite Force: cinematics

Elite Force's cinematics are Bink movies (`video/<name>.bik`), decoded by
Reluva's shared decoder ([bink.md](../../../../../common/video/bink.md)).

- **`cinematic <name>`** (`stvoy.exe` 0x404840) ends the game and plays
  `video/<name>.bik` full screen. When it ends, the engine runs the
  `nextmap` cvar's command and clears it.
- **`ingamecinematic <name>`** plays one over a running game. The game's
  scripts send it as `inGameCinematic <name>`.
- **At start-up** the engine plays `cinematic eflogo`.
- **Skipping.** A typed key (ASCII, Esc included) or the first mouse
  button skips a cinematic.
- **Variants the engine also looks for:** `_lo` versions on slow machines,
  and a German check. Reluva plays the plain name.
