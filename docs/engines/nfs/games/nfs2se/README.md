# Need for Speed II SE

Game id `nfs2se`, on the [Need for Speed engine](../../README.md); its own
code is in `src/engines/nfs/nfs2se/`, and what it shares with NFS III in
`src/engines/nfs/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| US CD (English and Spanish set-up, Windows), and an install from it; both executables built 23 September 1997 | `nfs2sen.exe` and the cars' data, `GameData/Sim/CarData/cardata.viv` (`detectionTable`, `src/engines/nfs/games.cpp`), the same in the install and on the CD | Recognised, not playable: it shows what was found of it |
| Other releases | `nfs2sen.exe` or `nfs2sea.exe`, with `cardata.viv` (`fallbackTable`): "unknown release" | Not checked |

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `nfs2sen.exe` | `0b44f8449fa8889ecd5c4d17b90a3a40` | 900096 |
| `GameData/Sim/CarData/cardata.viv` | `f7cf339beaa69552581e6e1a91a82ab0` | 242816 |

The install leaves the tracks on the CD, so they can't tell it apart;
the cars' data is in both. The CD's 3dfx program, `NFS2SEA.EXE`, is in
[research/first-survey.md](research/first-survey.md).

## Where it stands

- **Recognised** in its install and on its CD, and its CD's files found
  for an install.
- **Plays, being finished** (Phase 6 of [PLAN.md](PLAN.md)): the retail
  `nfs2sen.exe`, the software program, runs in the x86 emulator as NFS
  III's does: its movies, its front end with the keys and mouse, and
  races, about 39 frames a second on the machine measured
  ([research/hosted-spike.md](research/hosted-spike.md)). With
  `renderer=glide` the 3dfx program (`nfs2sea.exe`, the install's or
  the CD's) plays instead, on Reluva's Glide stand-in as a Voodoo
  Graphics. Two players race over the network (IPX), a serial link or a
  modem, as in NFS III ([research/network-play.md](research/network-play.md)).
  Without either program the game shows what was found of it
  (its folders, programs, music, movies, tracks and car models:
  `src/engines/nfs/nfs2se/status_page.cpp`).
- **Its files that NFS III's formats cover all read**: every archive,
  picture bank (311 on the CD), text table, font, sound bank, music
  stream and map, and the compressed `.qda` data, with NFS II SE's
  differences ([../../formats/](../../formats/)); and its own tracks,
  `.col` files, skies, lights, car models and movies
  ([formats/](formats/)).
- **The data viewer** has, beside the Status and Play pages, every track and car
  model in 3D (the Tracks and Cars pages), its pictures (the pause
  screens in the race's palette, as the software game draws them),
  strings, fonts, sounds, music, and movies with their sound. Its retail files were surveyed in
  [research/first-survey.md](research/first-survey.md).
- Plans, phases and decisions: [PLAN.md](PLAN.md).

## Using it

Add the install in the launcher (*Add Game...*), with a copy of its CD's
files: the game reads its tracks, music and movies from the CD. The copy
is found:

- where `cdpath=<folder>` in the game's section of `reluva.ini` says
  (relative to the install, or absolute);
- in the install itself, when the CD's files were copied into it, or
  the game was added from the CD;
- in a folder beside the install with the CD's `NFS2SEN.EXE` and its
  music (`GameData/Audio/pc`).

Without it, the game says where to put it. The keys and mouse go to the
game, Esc included; Ctrl+F5 goes back to the launcher. The game's
*Renderer* setting picks its program: *Voodoo on Glide*
(`renderer=glide` in `reluva.ini`) its 3dfx one, drawn on the GPU (or in
software with `glide=software`), the others its software one. `reluva-viewer
<its name in the launcher>` shows its files.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats/) | The game's own files: [trk.md](formats/trk.md) (tracks), [col.md](formats/col.md) (textures, objects, the virtual road), [hrz.md](formats/hrz.md) (skies and lights), [geo.md](formats/geo.md) (car models), [dct.md](formats/dct.md) (movies) |
| [research/](research/README.md) | Working notes: the first survey of the retail files and executables |
| [PLAN.md](PLAN.md) | The plan, its phases and decisions |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

The formats it shares with NFS III are the engine's
([../../formats/](../../formats/)): archives, pictures, text, fonts,
sound banks and music.

## Terms

- **SE:** Special Edition, the 1997 re-release of Need for Speed II with
  3dfx support, a bonus track and more cars.
- **nfs2sen, nfs2sea:** the game's two executables: software rendering,
  and 3dfx (Glide 2) rendering.
