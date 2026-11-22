# Need for Speed II SE: testing

## Automated checks

### Unit tests

In `tests/engines/nfs/unit/`, beside NFS III's, on installs and CDs the
tests build (`makeNfs2seInstall`, `makeNfs2seCd` in
`tests/engines/nfs/fixtures.h`; never the game's files):

| Suite | What it checks |
|---|---|
| `nfs.unit.detection` (its `nfs2se` cases) | The install and the CD recognised by the fallback table, the CD once though both its programs match, an install with only the 3dfx program, and not without the cars' data; the CD's copy found beside the install, by `cdpath`, and missing; the tracks (once each, from the install and the CD), the car models in `cardata.viv`, and which folder each program is in |

### The basic engine test

`nfs.engine`'s `nfs2se` case (`tests/engines/nfs/engine.cpp`), against
its record `tests/engines/nfs/data/engine-record-nfs2se.txt`: a small
install and a CD's copy beside it, recognised and found; the viewer
through its pages (Status, Sounds, Music); the game's Status page until
Esc; and the game without the CD's copy, which says where to put it.

### The game-files test

Not yet. It's to be registered as `nfs.nfs2se`, run with
`RELUVA_NFS2SE=<the install>` (docs/DEVELOPMENT.md, Engine tests).

### What the automated checks don't cover

That the real release is recognised by its hashes: checked by hand
([Last checked](#last-checked)).

## A playthrough by hand

1. Add the install in the launcher, with a copy of the CD beside it: it's
   found as "Need for Speed II SE".
2. Start it: the Status page lists the install, the CD's copy (beside the
   install), 19 music streams, 15 movies, `nfs2sen.exe` in the install
   and `NFS2SEA.EXE` on the CD, 8 tracks (`TR00`, `TR02` to `TR08`) and
   53 car models. Esc goes back.
3. Move the CD's copy away and start it again: it says the CD's files
   weren't found.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US CD and an install from it | Linux | The install and the CD recognised as the release (`reluva-tool detect`), and an install with only `NFS2SEA.EXE` as an unknown release; the Status page drawn from the install with the CD's copy beside it (`reluva-tool nfs page`), with the counts in [A playthrough by hand](#a-playthrough-by-hand) |
| November 2026 | US CD and an install from it | Linux | NFS III's readers over every file (`reluva-tool nfs survey`), and `nfs2sen.exe` loaded in the x86 emulator ([research/first-survey.md](research/first-survey.md)) |
