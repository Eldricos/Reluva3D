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
2. **The archive** (next): `DATA.dat` is a ZIP of 6,949 stored files whose
   end-of-central-directory record is signed `RE\5\6`, not `PK\5\6`.
   Teach `src/common/zip.cpp` to accept that signature (or to walk the
   local headers when the record is missing), so every engine gains it.
   `reluva-tool stawayteam files` lists it.
3. **The object text format**: most data (`Rules.txt`, `.zax` maps,
   `.MDS` missions, `.STY`, roster, items, damage and death types) is a
   tree of `ClassName { Key=Value ... }` blocks with `Array`s. One reader
   for all of them, a page `formats/object-text.md`, and a check that it
   reads every such file in the archive. `.RDS` (`:Label` then text, with
   `$trigger` and `{Inventory ...}` substitutions) gets its own reader.
4. **Pictures and the data viewer**: `.pal` (768 bytes, 8-bit), and the
   16-bit sprite formats `.frm16` (single frames, run-length coded),
   `.seq16` (animated textures) and `.mdl16` (models: named animations,
   directions, frames, sounds; 3,738 of them, 130 MB). Their layouts from
   comparing files and reading the loaders in `Star Trek.exe`. A
   `pages/` viewer for pictures, models by animation and direction,
   fonts (`Cache/Fonts`), sounds and the object trees.
5. **Sound and movies**: Ogg Vorbis for 1,484 files (music, effects,
   711 character barks, 403 voice-overs); see
   [Decisions](#decisions) for the decoder. Bink movies (all revision
   `i`) through the existing `src/video/bink.cpp`. Intro sequence:
   Activision, Paramount, Reflexive logos, then `Intro.bik`.
6. **Front end**: `Interface/Screens` (310 files: buttons, layouts,
   hover and press sounds) and the menu definitions in `Rules.txt`
   (`CStarTrekMainMenu`, `CAnimatedMenuInfo`...). Main menu over
   `Main Screen Background.bik`, then each screen, matched against
   recordings. Game area 640x480 (`Screen Pos=75,0`,
   `Screen Size=565,423` in `Rules.txt`).
7. **Maps**: a `.zax` level drawn (layers, placed models, textures,
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
   between Reluva and the original.
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
