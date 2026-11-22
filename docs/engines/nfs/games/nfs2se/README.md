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
- **Not playable yet.** The game shows what was found of it: its
  folders, its two programs, the music and movies, the tracks and the
  car models (`src/engines/nfs/nfs2se/status_page.cpp`).
- **The data viewer** has, beside that page, the pictures, fonts, sounds
  and music, through the readers it shares with NFS III; their NFS II
  differences are the plan's next phase. Its retail files were surveyed
  in [research/first-survey.md](research/first-survey.md).
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

Without it, the game says where to put it. The game itself, for now,
shows what was found (Esc goes back); `reluva-viewer <its name in the
launcher>` shows its files.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [research/](research/README.md) | Working notes: the first survey of the retail files and executables |
| [PLAN.md](PLAN.md) | The plan, its phases and decisions |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

The formats it shares with NFS III are on NFS III's pages until each is
checked on both games' files ([PLAN.md](PLAN.md), Phase 2).

## Terms

- **SE:** Special Edition, the 1997 re-release of Need for Speed II with
  3dfx support, a bonus track and more cars.
- **nfs2sen, nfs2sea:** the game's two executables: software rendering,
  and 3dfx (Glide 2) rendering.
