# Star Trek: Away Team: plan

## Goal

The US English retail release of Star Trek: Away Team (Reflexive
Entertainment / Activision, 2001), played from the player's own install:
the front end (main menu, roster, team selection, briefing and
debriefing, the computer database, options, key setup, load and save),
all 18 missions with their scripts, AI, dialogue, voice and music, and
the two-player cooperative LAN game. Rebuilt in C++ from the game's data
and from reading `Star Trek.exe`; the original program isn't run.

## Phases

Each phase ends with something that runs and is committed.

1. **Engine start** (done): the engine from the template, its pages, a
   row in the games table, and a first survey of the files
   ([research/file-survey.md](research/file-survey.md)). Detection on
   `Star Trek.exe` and `DATA.dat`, with an "(unknown release)" fallback
   by the files' names; *Import Game...* from the CD's loose `Setup/`
   files (the CD holds the install uncompressed as well as in
   `data1.cab`).
2. **The archive** (done, [formats/data-dat.md](formats/data-dat.md)): `DATA.dat` is a ZIP of 6,949 stored files whose
   end-of-central-directory record is signed `RE\5\6`, not `PK\5\6`.
   `src/common/zip.cpp` takes that signature, as the game's own reader
   does; `reluva-tool stawayteam data-list` and `data-extract` read it.
3. **The object text format** (done,
   [formats/text-object.md](formats/text-object.md),
   [formats/rds.md](formats/rds.md)): most data (`Rules.txt`, `.zax` maps,
   `.MDS` missions, `.STY`, roster, items, damage and death types) is a
   tree of `ClassName { Key=Value ... }` blocks with `Array`s. One reader
   for all of them, a page `formats/object-text.md`, and a check that it
   reads every such file in the archive. `.RDS` (`:Label` then text, with
   `$trigger` and `{Inventory ...}` substitutions) gets its own reader.
4. **Pictures and the data viewer**, in two parts:
   - **Frames and palettes** (done): `.frm16` pictures, every one of the
     671 decoded ([formats/frm16.md](formats/frm16.md)); the palette and
     colour tables described, not read, as nothing 8-bit is drawn
     ([formats/pal.md](formats/pal.md)); the data viewer's Pictures page.
   - **Objects saved in binary** (done,
     [formats/binary-object.md](formats/binary-object.md)): `.seq16`
     (animated textures and fonts), `.mdl16` (models: their entities,
     animations, directions, frames; 3,738 of them, 130 MB) and the
     maps' `.way`, the same objects as the text files saved with no
     names. Every class's properties are read out of the executable
     ([research/classes.txt](research/classes.txt)), and with them Reluva
     reads every such file to its last byte.
   - **The viewer's other pages** (done): sequences, models by animation
     and direction, fonts, sounds and the objects.
5. **Sound and movies** (done, [systems/startup.md](systems/startup.md)):
   Ogg Vorbis for 1,484 files (music, effects, 711 character barks, 403
   voice-overs) through libvorbis ([Decisions](#decisions)); Bink movies
   (all revision `i`) through the shared `video::MoviePlayer`. The game
   plays its opening movies (Activision, Paramount, Reflexive, then
   `Intro.bik`), then the main menu's screen with its movies and the
   startup song, without the menu.
6. **Front end** (under way, [systems/menus.md](systems/menus.md): the
   main menu's buttons, the credits, the difficulty window and the
   confirm dialog, the settings; the briefing,
   [systems/briefing.md](systems/briefing.md); the team selection,
   [systems/team-selection.md](systems/team-selection.md)): `Interface/Screens` (310 files: buttons, layouts,
   hover and press sounds) and the menu definitions in `Rules.txt`
   (`CStarTrekMainMenu`, `CAnimatedMenuInfo`...). Main menu over
   `Main Screen Background.bik`, then each screen, matched against
   recordings. Game area 640x480 (`Screen Pos=75,0`,
   `Screen Size=565,423` in `Rules.txt`).
7. **Maps** (under way, [systems/maps.md](systems/maps.md): a map's
   ground, polygons and models drawn and scrolled, the interface around
   it, the team walking by the waypoints, the triggers, the enemies,
   fighting, noise; the objectives and a mission's end,
   [systems/objectives.md](systems/objectives.md); items,
   [systems/items.md](systems/items.md)): a `.zax` level drawn (layers, placed models, textures,
   water warp effects, minimap), scrolled with the camera bounds, with
   its `.way` file (binary, 33 of them, likely the walk grid and
   waypoints) worked out for movement. First target: Mission 01, Klingon
   Outpost, walkable with one officer.
8. **Rules**: the team (attributes Speed, Stealth, Stamina; ranks,
   fields), orders and pathfinding, stealth and noise (crouch, walk, run
   modifiers are in `Rules.txt`), line of sight, combat and damage
   types, inventory and items, the enemy AI, and the missions' triggers
   and actions (`CCameraScript`, `CInfoDialogAction`,
   `CMultipleActionsAction`...; the executable holds some 600 class
   names). Read from the executable, checked
   against recordings and against the original running.
9. **Saves**: the original's save files read and written, so saves move
   between Reluva and the original. The game writes ZIP archives of its
   own, signed as `DATA.dat` is: likely its saves
   ([formats/data-dat.md](formats/data-dat.md#behaviour)).
10. **Multiplayer**: the two-player LAN co-op game. The original uses
    DirectPlay (TCP/IP or IPX, LAN only); Reluva's uses its own
    network code (`src/common/net.h`), between copies of Reluva.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | Engine id `stawayteam`, one game, no `games/` folder | Follows `stgenerations` and `stharbinger`; no other game on this engine is planned yet. Moves to the family layout if one is added |
| November 2026 | A native rebuild, not running `Star Trek.exe` under `src/emulation/x86/`, from the start | The game is data-driven (rules, maps, missions as readable object trees), 2D (DirectDraw only, no Direct3D), and its executable has no copy protection, so it can be read statically. Hosting it first would reach a first run sooner, but little of that work would carry over to the rebuild, and its DirectPlay and DirectSound needs would go into stand-ins for a route then left. To compare with the original, it's run on Windows (a virtual machine) instead |
| November 2026 | Ogg Vorbis through Xiph's libvorbis, downloaded and built by CMake ([Ogg Vorbis](../../common/audio/vorbis.md)) | The game's sounds use floor 0, from before Vorbis 1.0, which stb_vorbis refuses ([research/file-survey.md](research/file-survey.md#sound)). The reference decoder takes them, is BSD-licensed, and was far less work than a decoder of Reluva's own |
| November 2026 | The CD check isn't kept | Reluva never needs the CD in the drive; goes in `KNOWN_DIFFERENCES.md` |
| November 2026 | The classes' properties read out of the executable by a script, kept as a data file in `research/`, and a C++ table generated from it | The binary files can't be read without them, and there are 553 classes; a script makes them again from any copy, and the files' versions check them. A model's entity can be of any of some 150 classes, so the engine needs them all, not a few written by hand |
