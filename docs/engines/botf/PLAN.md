# Star Trek: Birth of the Federation: plan

## Goal

The English retail release (and the German, once it's hashed) played from
its own files: the galaxy, the five empires and the minor races, the
turn's production, research, growth and morale, diplomacy and
intelligence, and ship combat, as the original plays them.

## Phases

Readers and a data viewer first, then the game's screens, then its rules,
with recordings of the original filling what the data can't show.

1. **Detection** (done): the English release; the German one's
   `german.snd` is to be hashed when a German install is available.
2. **Archives** (done): `stbof.res`, `alt.res` and the `.snd` archives.
3. **Data viewer** (done): pictures, fonts, sounds, speech, music, text
   tables, data tables and window layouts.
4. **The data tables' numbers** (done but for a few fields): every table's
   layout, its fields named from the manual, a recording and the modding
   community's work, each checked against the data
   ([formats/tables.md](formats/tables.md)).
5. **The first screens** (done): the main menu, game setup and empire
   selection ([systems/screens.md](systems/screens.md)).
6. **Models:** the `.hob` models with their `.gif` textures, on a
   Models page with `src/graphics/gfx3d.h`.
7. **Movies:** the CD's AVI movies (Cinepak video), through the shared
   video code: the opening movie is done; the empires' wait for a game to
   start.
8. **Saves** (next): the `.sav` format, worked out by comparing saves
   taken a turn apart in the running game: the map of the game state the
   rules act on. The archive's files are listed; the galaxy (its sectors,
   star systems, planets, buildings, other objects and outposts), the
   empires, the races, the treaties, the ships and task forces and the
   last turn's results are read ([formats/sav.md](formats/sav.md)), into
   one game state ([systems/game-state.md](systems/game-state.md)). Next:
   writing it back.
9. **Screens:** the main menu's own screens are done (the options, Load
   and Save Game's lists, the Hall of Fame, Multiplayer Setup). The galaxy
   screen's frame, map, empire's panel and selected sector's strip are
   drawn from a save ([systems/galaxy-screen.md](systems/galaxy-screen.md));
   it zooms; next its other buttons and later turns' maps, then the system, empire and diplomacy
   screens, from their layouts and recordings of the original.
10. **Rules:** turn processing (production, research, growth, morale, the
    AI) and ship combat, from the data tables, documentation and
    measurements in the running game, checked against saves. By far the
    largest part, as for any 4X game.

## Decisions

| Date | Decision | Why |
|---|---|---|
| October 2026 | Detection keys on the data files (`stbof.res`, the speech archive), not on `trek.exe` | `trek.exe` is only SafeDisc's loader ([formats/executable.md](formats/executable.md)); a patched or replaced one shouldn't stop a copy being recognised |
| October 2026 | Not decrypting `Trek.icd` | It needs the CD and the SafeDisc driver, and getting round the protection is a legal question for each contributor; the data files, the manual, recordings and published write-ups are enough |
| October 2026 | The modding community's editor (UE, GPL-2.0) is read for file layouts only, and every field it gives is checked against the data | It's public documentation of the formats, two decades of the community's work; no code is copied |
| November 2026 | The unofficial "Joker patch" isn't used, to run the game or to read its code | It removes SafeDisc; the original, unpatched executable from the CD (Windows 98 or XP, a real machine or a virtual machine with the CD passed through) is used instead |
| November 2026 | The engine moved from the ReluvaBOTF repository to this one | This repository is where Reluva's work continues; the engine was brought into its layout, names and documentation standard |
