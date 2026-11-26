# Mysteries of the Sith: plan

## Goal

The US retail release played from its CD: the 14 single-player levels
with their cut scenes, the menus, saves, and its multiplayer episodes,
as Jedi Knight is played ([jk/PLAN.md](../jk/PLAN.md)).

The copy studied, and what each file holds, is in the
[README](README.md#the-install-and-the-cd).

### How

Hosted, as Jedi Knight is: the retail `JKM.EXE` run in Reluva's x86
emulator. Jedi Knight's gate chose that for the engine
([jk/PLAN.md](../jk/PLAN.md#the-gate)), and a trial showed
`JKM.EXE` needs nothing new of the emulator to reach a level
([README](README.md#where-it-stands)): it imports the same DirectX 5
interfaces, WINMM's CD audio and joysticks, and plays its cut scenes
itself. What's new is Reluva's side: knowing the game, making its
install from the CD's image, and the game's own addresses where the host
reaches into the program.

## Phases

1. **The game known to Reluva** (done). Game id `mots`; its release recognised
   by `JKM.EXE` and two archives, others by the names alone; the host
   made one for both games, each game's program, registry key and CDs
   given with it rather than Jedi Knight's written in: its CD check hooked
   at `0x410db0` to put the CD asked for in the drive, as Jedi Knight's
   is; its own CD told by its marker.
2. **Import from the CD or its image** (done). *Import Game...* from the
   CD, a copy of its files, or its image (a cue sheet and its tracks):
   the files the installer copies, the cut scenes and the marker kept
   under `cd1/`, and the music, as for Jedi Knight. Reading an image's
   data track (ISO 9660, `src/common/iso9660.h`) is new shared code;
   Jedi Knight's CDs can be imported from their images with it too.
   Jedi Knight's CD 2 for the game's one check of it
   ([README](README.md#proof-of-owning-jedi-knight)): its marker taken
   from *Add other disc...* (the CD, a copy or an image)
   ([README](README.md#using-it)). Imported from the CD's image, the
   game started, checked both CDs, and reached its first level with its
   music.
3. **Play** (done). The trial made the game: its window, sound, music, the
   pointer and mouse, the joysticks, saves in the save folder, the
   registry kept; the engine's options carried over where `JKM.EXE` has
   the same code at its own addresses (the world moved at every frame,
   *Setup*'s settings saved when Reluva is closed, Direct3D on the
   graphics card); multiplayer through the emulator's DirectPlay. Each
   checked running ([README](README.md#where-it-stands)); the addresses
   are in the [README](README.md#where-reluva-reaches-into-the-program).
4. **Every level and cut scene.** Gone through with the game's cheats,
   as Jedi Knight's were ([jk/TESTING.md](../jk/TESTING.md)).
5. **The data viewer.** The `.goo` archives read as `.gob`; each kind of
   file checked on every file for what differs from Jedi Knight's (the
   levels, models, `.per` files, AI); and the SMUSH cut scenes decoded,
   a new video format for Reluva, needed only by the viewer since the
   game plays them itself.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | Game id `mots`, on the `sith` engine beside `jk` | The same engine and files, an expansion of Jedi Knight's |
| November 2026 | Hosted, as Jedi Knight | Jedi Knight's gate chose it for the engine; `JKM.EXE` reached its first level on the emulator as it is |
| November 2026 | The game's check of Jedi Knight's CD 2 answered with that CD's own marker, linked with *Add other disc...* when the game is imported, as its `cd2`; never by writing `InstallData` for it | The check is the game's proof that its owner has Jedi Knight; answering it with the CD is what a player did. The import is where the person has their CDs to hand, and the game needs it on its first start |
| November 2026 | Only Jedi Knight's CD 2's marker is copied for Mysteries of the Sith, not its cut scenes or music | The game reads nothing else from it |
| November 2026 | The CD check made to search from the first drive each time | Right after Jedi Knight's CD 2 it searches from the drive after it, so with Reluva's one drive it would ask for its own CD once more; a player swapping CDs pressed *Ok* there for the same result |
| November 2026 | Each program's addresses in one table in the host, checked against its bytes before any change | One host for both games; a release whose bytes differ is left as it is, as Jedi Knight's were |
| November 2026 | Disc images read by Reluva itself (ISO 9660 on a cue sheet's data track or an `.iso`), for importing | Players keep their CDs as images; the cut scenes are on the CD only, and an image holds its music too |
