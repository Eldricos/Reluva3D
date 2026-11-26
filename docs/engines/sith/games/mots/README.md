# Jedi Knight: Mysteries of the Sith

Game id `mots` (to be), on the [Sith engine](../../README.md), Jedi
Knight's expansion; its code will be in `src/engines/sith/`, with
[Jedi Knight's](../jk/README.md).

## Releases

| Release | Recognised by | State |
|---|---|---|
| US retail, one CD (English, Windows); `JKM.EXE` linked 16 January 1998 | Not yet; to be `JKM.EXE`, `Resource/JKMRES.GOO` and `Episode/JKM.GOO` | Run by hand in the emulator ([Where it stands](#where-it-stands)) |

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
does. Its check looks for a CD from the drive after the last one it
found, so the first time, right after Jedi Knight's CD 2, it asks for
its own CD, and finds it once *Ok* is pressed *(confirmed)*.

## Where it stands

- **Detection, import, the viewer:** none yet ([PLAN.md](PLAN.md)).
- **The game:** a trial, with Jedi Knight's host changed by hand for
  `JKM.EXE` (its name, its registry key, and the CD check at
  `0x410db0` putting Jedi Knight's CD 2 in the drive for argument 2 and
  its own CD for the rest), ran it in Reluva's x86 emulator with nothing
  new in the emulator *(confirmed)*: the CD checks above, the opening
  (`jkmintro.san`, picture and sound), the menus, and the first level
  (`s1l1_rebelbase.jkl`) starting with its scene in the rebels' base,
  drawn through Direct3D, at 200 to 250 million instructions a second.

## Using it

Not yet: the game isn't recognised or imported ([PLAN.md](PLAN.md)).

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
