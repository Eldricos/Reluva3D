# How Reluva's Star Trek: New Worlds support was worked out

Reluva reads Star Trek: New Worlds' files and plays its missions from the
player's own install, with none of the original's code in it. This page says
how the facts behind that were found, so they can be checked again or
extended. The facts themselves are in this folder's format and game pages
([README](README.md#the-docs)), each with its evidence.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository: the mission scripts name the game's
  strings by their id rather than copying them
  ([mission-scripts.md](mission-scripts.md)), and the recordings of play and
  the game's wording stay out ([play.md](play.md)).
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not be
  used.

## Sources

- **The game's data files**, read directly: the archives
  ([cat.md](cat.md)) and what they hold (models, maps, textures, fonts,
  strings, sounds, the mission list). Each format page has what showed its
  layout (for example [geo.md](geo.md#evidence)).
- **Recordings of the original being played**, for how the game behaves:
  the campaigns' order, the briefings, the messages, the controls, the
  starting colonies. A page per mission
  ([play.md](play.md)); what they established, in
  [play.md](play.md#what-they-established).
- **Not yet: the executable.** The missions' logic, where the starting
  colonies go and how the interface is laid out are not in the data files,
  presumably in `STNW.exe` ([mission-scripts.md](mission-scripts.md),
  [colony.md](colony.md)). It hasn't been read; what Reluva has of these
  is built from the recordings.
- **Next: playing the original in a VM**, to fill the gaps the recordings
  left: [vm-checklist.md](vm-checklist.md).

## The tools

`reluva-tool newworlds <command>`: `cat-list`, `cat-probe` and `cat-extract`
for the archives, and `geo-*`, `gmf-*`, `tex-*`, `stb-dump`, `fif-info`,
`mdl-info`, `lbp-info`, `ibi-dump` and `level-info` for what's in them. The
engine's data viewer ([README](README.md#using-it)) shows the same files.

## Checking the work

- **The archives:** `cat-probe` walks every archive's table by the rules in
  [cat.md](cat.md#verified); all 24 of the retail install pass.
- **The formats:** each reader checks its own layout as it reads (for
  example every `.geo` edge must lie on both its faces,
  [geo.md](geo.md#evidence)).
- **The mission scripts:** each recorded mission's script is played through
  in the order its recording showed, and must end in a win where the
  recording did ([mission-scripts.md](mission-scripts.md#how-far-they-go)).
- Tests: [TESTING.md](TESTING.md).
