# Jedi Knight: Mysteries of the Sith

Game id `mots`, on the [Sith engine](../../README.md), Jedi Knight's
expansion; its code is in `src/engines/sith/`, with
[Jedi Knight's](../jk/README.md).

## Releases

| Release | Recognised by | State |
|---|---|---|
| US retail, one CD (English, Windows); `JKM.EXE` linked 16 January 1998 | `JKM.EXE`, `Resource/JKMRES.GOO` and `Episode/JKM.GOO` (`detectionTable`, `src/engines/sith/games.cpp`) | Recognised, imported from its CD or its image with Jedi Knight's CD 2; plays from the import ([Where it stands](#where-it-stands)) |
| Other releases | The same three files present (`fallbackTable`): "unknown release" | Not checked |

Signatures, for detection (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `JKM.EXE` | `1ef9c284fb4fa9df3490b767061aed8b` | 1759744 |
| `Resource/JKMRES.GOO` | `d3c788a2e4afb4a5fc124adf2481cb3a` | 86091024 |
| `Episode/JKM.GOO` | `1cc4e2488f355352b70f2102f0f1546b` | 36273376 |

### The install and the CD

One CD, `SITH`: a data track and 13 audio tracks (2 to 14, 2 to 4
minutes each), the music. Its `GAMEDATA` folder is laid out as Jedi
Knight's CD 1's, and the installer copies from it: the install's files
are the same bytes (whole-file MD5s compared).

| In the install | On the CD | Notes |
|---|---|---|
| `JKM.EXE`, `OLHOOK.DLL` | `GAMEDATA/EXE/` | The game; `OLHOOK.DLL` isn't imported by it |
| `Resource/JKMRES.GOO` | `GAMEDATA/RESOURCE/JKMRES.GOO` | Textures, models, animations, scripts, AI, colormaps (5,897 files) |
| `Resource/JKMsndLO.goo` | `GAMEDATA/MININSTALL/JKMSNDLO.GOO` | The interface's pictures and fonts, sounds and speech (1,222 files) |
| `Episode/JKM.GOO`, `JKM_KFY.GOO`, `JKM_MP.GOO`, `JKM_SABER.GOO` | `GAMEDATA/EPISODE/` | Single player (14 levels), and three multiplayer episodes |
| `Controls/*.CTM` | `GAMEDATA/CONTROLS/` | Control set-ups for 15 joysticks and pads |
| — | `GAMEDATA/RESOURCE/VIDEO/*.SAN`: 24 | The cut scenes, read from the CD while playing: LucasArts' own SMUSH format, not Smacker |
| — | `GAMEDATA/RESOURCE/JK_.CD` | The CD's marker, `29 69 42 3b`; Jedi Knight's CDs have their own ([jk-cd.md](../jk/formats/jk-cd.md)) |
| — | `GAMEDATA/RESOURCE/VIDEO/CUTSCENES.ZIP`, `UNSUPPORTED.ZIP` | Extras for players: an archive of the cut scenes' scripts (`cutscenes.goo`), and an unfinished level (`S6L1`) with its scripts |

The `.goo` archives are [GOB archives](../jk/formats/gob.md) by another
name (`GOB ` and the same directory). The rest of the install is the
launcher (`Sith.EXE`), the readme and troubleshooting guide, and links
to the publisher's web site and game service. There's no
`SMACKW32.DLL`: the game plays its SMUSH cut scenes itself.

### Proof of owning Jedi Knight

`JKM.EXE` asks once for Jedi Knight's CD 2 *(confirmed)*. As it starts
(`0x410cc0`) it reads the value `InstallData` from its registry key
(`HKEY_LOCAL_MACHINE\Software\LucasArts Entertainment Company
LLC\Mysteries of the Sith\v1.0`). Where there's none it asks, in its
own box, for *your original Jedi Knight CD number 2*, and checks for it
with the CD check (`0x410db0`, its first argument 2) by that CD's
`JK_.CD`; then it keeps, as `InstallData`, a value made from that CD's
marker and Windows' product ID (`0x422440`, `BadInstall` where there's
none). From then on, at each start, it checks its own CD (the check's
first argument `0x13`) against the value got back from `InstallData`,
so the check passes only with Mysteries of the Sith's CD, once Jedi
Knight's has been shown.

The installer writes no `InstallData` (none in its script); the game
does. Its check looks for a CD from the drive after the letter it's
given, the last drive it found one in, so the first time, right after
Jedi Knight's CD 2, it asks for its own CD, and finds it once *Ok* is
pressed *(confirmed)*.

In Reluva, Jedi Knight's CD 2 is linked when the game is imported, with
*Add other disc...* ([Using it](#using-it)): its marker is kept as the
install's `cd2`, and the CD check (hooked at `0x410db0`) puts it in
drive D when the game asks for it, and the game's own CD the rest of the
time; it also has each search start from the first drive, so the game
finds its own CD straight after Jedi Knight's, without asking *(confirmed)*.
The game keeps `InstallData` in its registry (the save folder's
`registry.txt`), so it doesn't ask again.

## Where it stands

- **Detection:** the US release is recognised from its install; other
  releases by their files' names.
- **Import:** *Import Game...* makes an install from the CD, a copy of
  its files, or its image (the files taken off the image's data track),
  with its cut scenes and music; Jedi Knight's CD 2 is added with *Add
  other disc...* for the game's check of it.
- **The game:** played. The retail `JKM.EXE` runs in Reluva's x86
  emulator, as Jedi Knight's does, with nothing new in the emulator.
  Imported from the CD's image, its first start checks Jedi Knight's CD 2
  and its own, plays the opening (`jkmintro.san`, picture and sound),
  the menus, and a new game's cut scene and first level
  (`s1l1_rebelbase.jkl`), its music asked of the CD *(confirmed)*. A game
  saved from the in-game menu (`player\<name>\msav0000.jks`) is listed
  by *Load Game* and loads; the autosave is made as a level starts. Its
  Direct3D drawn on the graphics card matches its own drawing (154
  scenes, 0.0013% of pixels differing at most). Two copies played
  multiplayer on one machine: one hosted *Kill The Fool With The
  Ysalamiri*, the other listed it, joined with a character of its own,
  and each saw the other's in the level *(confirmed)*. The whole
  single-player episode skimmed with its cheats reached every level and
  cut scene, the finale and the credits ([TESTING.md](TESTING.md#last-checked)). The engine's
  settings act on it as on Jedi Knight ([Where Reluva reaches into the
  program](#where-reluva-reaches-into-the-program)).
- **The data viewer:** reads its archives (`.goo`, the `.gob` format) and
  every file in them as Jedi Knight's ([Its files](#its-files)), and shows
  them as it does Jedi Knight's; its levels drawn from the player's
  start, lit by their coloured light as the game's Direct3D lights them
(or, with C, as its software renderer does). Its cut scenes play
  on the Cut scenes page with their sound and subtitles: SMUSH, decoded
  by Reluva ([smush.md](../../../../common/video/smush.md)), each frame
  the same as the game shows it *(confirmed)*.

### Its files

The archives hold Jedi Knight's formats, read by the same readers: of
7,119 files, every one reads (`reluva-tool sith formats`) but two
materials the game can't load either, as its loader takes only `MAT `
version 50 (`0x44f6df`) *(from the code)*: `00skyline.mat` is empty and
`01narsky.mat` is version 10. What differs:

- **Levels:** four lights for each corner of a surface, a light and its
  red, green and blue ([jkl.md](../jk/formats/jkl.md#in-reluva)).
- **Three odd files** the game reads anyway, and the readers now read as
  it does: a sound class's flag written `x0` and a line missing its `#`
  ([snd.md](../jk/formats/snd.md#in-reluva)), and an animation with a
  stray line after its nodes ([key.md](../jk/formats/key.md#in-reluva)).
- **Cut scenes:** SMUSH (`.san`), with codec 48 pictures, 640 by 350
  (the opening 640 by 300), 12 frames a second, and sound
  ([smush.md](../../../../common/video/smush.md)). Their subtitles are
  `COG_` strings, in `ui\jkstrings.uni` or the episode's
  `misc\cogstrings.uni`, named by a `TRES` chunk's number (`0x4247fc`):
  its last four digits, after a first digit its ten-thousands pick, 7
  for 1 (the levels' openings, `S1L1OCS.SAN`'s 11101 is `COG_71101`), 9
  for 2 (their endings, 21101 is `COG_91101`), else 8 (`S5L3ECS.SAN`'s
  5301 is `COG_85301`), but 5 for 166 and 148 (`COG_50166`, "There he
  is. Stop him!"). Every one of the 148 numbers in the 21 files with
  lines names a string so. A line starting `|` (Huttese, translated)
  always shows, the rest only with the player's *full subtitles*
  (`fullsubtitles` in the player's `.plr`) (`0x424871`). The game draws
  them centred below the picture, from row 385, in the subtitle font,
  broken into lines as Jedi Knight's are *(confirmed: seen in the game
  with full subtitles, and the viewer's the same pixels)*.

### Where Reluva reaches into the program

As for Jedi Knight ([../jk/systems/hosting.md](../jk/systems/hosting.md)),
the host changes `JKM.EXE` in a few places, each only where the bytes
there are the US release's (`kPrograms`,
`src/engines/sith/host/game_process.cpp`):

- **The CD check** (`0x410db0`): the CD asked for put in drive D first
  ([Proof of owning Jedi Knight](#proof-of-owning-jedi-knight)).
- **The world's moves** (*Frame rate* other than *As the game*): the
  game moves its world only when more than 20 ms have passed since it
  last did, in each of its three kinds of frame (the checks at
  `0x403cd6`, `0x404302` and `0x40455f`, then `0x510b80`); Reluva makes
  that more than 9 ms, as for Jedi Knight. Its clock takes steps of 10
  to 500 ms (`0x510d60`, the bounds at `0x5a1ef8` and `0x5a1ef4`,
  changed only by two command-line options at `0x401760`). At 100 frames
  a second the world moved 354 times in a stretch of play where it moved
  159 and 169 times without *(confirmed)*.
- **Widescreen** (the *Widescreen* setting): the call of the tangent
  in its focal length (`0x447e26`, in `0x447db0`, to `0x436386`) made to
  a trampoline that widens the view by the screen's shape, as for Jedi
  Knight ([../jk/systems/hosting.md](../jk/systems/hosting.md), step
  24). Its scripts set the field of view again at each level's start
  from their zoom (`0x513670`), so the cameras' own 90° can't be the
  place: the focal length, worked out from whatever they set, is
  *(confirmed)*. The scope's sight, scaled by the screen's width across
  and its height down, is scaled by its height both ways: the loader's
  scale read at `0x4124f6` from `0x5b08e4` rather than `0x5b08e0`.
- ***Setup*'s settings saved** when Reluva is closed with the game
  running: its own saving of them to the registry (`0x417560`, called
  from its end at `0x4013d5`, as Jedi Knight's at `0x4148f0`) run for it
  *(confirmed)*.

### Its cheats

Typed as Jedi Knight's are, through the chat key (T) in a level. The
words aren't kept as text: each is stored with every byte XORed with
all the bytes before it and with `0xda`, and what's typed is encoded the
same way and looked up (`0x4231f0`) *(from the code)*. Sixteen, tied to
their handlers as the console starts (`0x422500` to `0x422a2f`):

| Word | Handler | What it does |
|---|---|---|
| `statuesque` | `0x423450` | The game's name for it is *Disable AI* |
| `freebird` | `0x423480` | The game's *Fly mode on* *(confirmed)* |
| `boinga` | `0x4234b0` | Invulnerable, the game's *Invul* *(confirmed)* |
| `youarehere <x> <y> <z>` | `0x4234e0` | Moves the player to that place (`%f %f %f`, `0x53d530`): `youarehere 0 0 0` put Mara outside the level, where she died and the level began again *(confirmed)* |
| `makeitwork <n>` | `0x423530` | Sends thing *n*'s script a message (`0x53d680`, `0x52ae60`); with a number it ran and the game went on *(confirmed)*. Typed alone it stops the game: the text after the word is then null, and the program reads its number with its own `sscanf` (`0x56c350`, the read of address 0 at `0x56c365`) without the check `takemeto` makes (`0x42340d`) *(from the code)*: a fault of the program's, as on Windows |
| `gospeedgo` | `0x423560` | The game's *Slowmo* |
| `quickzap <n>` | `0x423590` | Puts the player where player *n*'s slot keeps its place (`0x54de80`: the position, orientation and sector at `+0x135c` and `+0x138c` of the 5,044-byte slots at `0x9a9fa0`, those in use): `quickzap 1`, after walking, put Mara back at the level's start; 10 and 40 did nothing in single player *(confirmed)* |
| `gameover` | `0x4235c0` | Ends the level (`0x403160`) *(confirmed)* |
| `diediedie` | `0x4235e0` | Every weapon, the game's *All Weapons* *(confirmed)* |
| `gimmestuff` | `0x423870` | The game's *All items* *(confirmed)* |
| `iamagod` | `0x423ba0` | The game's *Uber Jedi* *(confirmed)* |
| `trainme` | `0x423f00` | The game's *Level Up* *(confirmed)* |
| `trixie` | `0x424180` | The game's *Mana* *(confirmed)* |
| `morelife` | `0x4240f0` | The game's *Heal* *(confirmed)* |
| `cartograph` | `0x424140` | The game's *All Map* *(confirmed)* |
| `takemeto <n>` | `0x423400` | Goes to level *n* of the episode (`0x4035d0`): `takemeto 12` went to the Upper Sith *(confirmed)* |

## Using it

Make the install from the CD with *Import Game...*: choose the CD, a
folder with a copy of its files, or a folder holding its image (the
cue sheet with its `.bin` files, or an `.iso`). The files the installer
copies are taken off it, its cut scenes go to `cd1`, and, from an image,
its music (the cue sheet and the audio tracks' `.bin` files, not the
data track's) goes with them.

Then **link Jedi Knight's CD 2 with *Add other disc...*** before
importing: the CD, a copy of its files, or its image. Mysteries of the
Sith is an expansion, and asks once, on its first start, for *your
original Jedi Knight CD number 2* ([Proof of owning Jedi
Knight](#proof-of-owning-jedi-knight)). Only that CD's marker
(`GAMEDATA/RESOURCE/JK_.CD`) is copied, to `cd2`; the import's note
says when it's missing. Without it the game asks for the CD and can
only be cancelled. `reluva-tool import <the CD's folder> <folder> -patch
<Jedi Knight's CD 2>` does the same.

As for Jedi Knight, the CDs are found in the install or beside it, or
where the `cd1path` and `cd2path` settings say
([../jk/README.md](../jk/README.md#using-it)); here `cd2path` names Jedi
Knight's CD 2. `reluva-tool sith folders <install>` shows what's found.

*Level select*, as for Jedi Knight, shows the game's debug menus:
"(Debug)" on *Single Player*, to start any level of any episode, *Setup*'s
*Advanced*, and *Cutscenes* on the main menu, which the game otherwise
leaves out ([../jk/systems/hosting.md](../jk/systems/hosting.md), step
25).

## This folder

| Page | What's in it |
|---|---|
| This page | The release, where it stands |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **SMUSH:** LucasArts' format for its cut scenes (`.san`), of the
  1990s; Jedi Knight's were Smacker instead.
