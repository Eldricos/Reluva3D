# Mysteries of the Sith: testing

## Automated checks

### Unit tests

The engine's, shared with Jedi Knight ([../jk/TESTING.md](../jk/TESTING.md)),
check this game too:

| Suite | What it checks |
|---|---|
| `sith.unit.detection` | Its install recognised by its program and archives |
| `sith.unit.folders` | Its CD and Jedi Knight's CD 2 told apart by their markers, as its CDs 1 and 2; found for its install |
| `sith.unit.import` | Imported from its CD's image (a cue sheet, a raw data track and an audio track built in the test), Jedi Knight's CD 2 added with *Add other disc...*; what's made recognised, its CDs and music found |
| `common.unit.files` | Disc images read: ISO 9660 on a raw track through a cue sheet and on an `.iso` |
| `common.unit.video` (smush) | A SMUSH file built in the test: runs and a table of averages, blocks moved, filled and averaged, a palette fade, sound split across chunks, a subtitle |
| `sith.unit.levels` (coloured light) | Surfaces with four lights a corner |
| `sith.unit.render` (coloured light) | A polygon lit by colour: each channel of the texel times the corners', made a byte, held at 1 |
| `sith.unit.formats`, `sith.unit.models` | Sound class numbers as the game reads them; an animation's lines after its nodes left alone |

### The basic engine test

The engine's, `sith.engine`, on Jedi Knight's files.

### The game-files test

`sith.mots.gamedata.*` (`tests/engines/sith/gamedata_mots.cmake`), with
`RELUVA_MOTS` naming an install as *Import Game...* makes it; records in
`tests/engines/sith/data/gamedata/mots-us-1.0/`:

| Step | What it checks |
|---|---|
| `release` | The release recognised, its CD and Jedi Knight's CD 2 found, its 13 music tracks |
| `formats` | Every file in the archives decoded, each kind as a checksum; the two materials the game can't load either failing |
| `movies` | Every cut scene's frames, palettes, sound and subtitle numbers, as a checksum |
| `start` | The retail `JKM.EXE` run in the emulator from a new state for 30 seconds, the opening skipped, until it waits at *New Player*: its checks of Jedi Knight's CD 2 and its own (its `InstallData` written), the files it opens, its registry writes and debug output, as a checksum |
| `level` | The same with a player made (`mara`), *Single Player* and *New Game* clicked at set seconds, and `-skim`: the opening and the first level's cut scenes reached, the level ended by its cheat, and every file opened up to the second level, `s1l2_astcrust.jkl` |

Like Jedi Knight's, the two host steps record only what a run decides
itself: past the level a step stops at, neither the levels and cut
scenes reached nor the debug output count, so a faster or slower machine
gives the same record (the four host steps, Jedi Knight's and these,
passed run side by side on four cores).

### What the automated checks don't cover

Beyond the first level, running the game: its cut scenes, levels and
music, saves, the graphics card and multiplayer, by hand with
`reluva-tool sith host`, and the whole episode skimmed with `-skim`
([Last checked](#last-checked)).

## A playthrough by hand

Not yet by a person. To skim the single-player episode unattended, from
an imported install whose save folder has a player:

```bash
reluva-tool sith host <install> -write <save folder> -seconds 1500 -skim -keys "25:esc,28:enter,31:click318x217,34:click295x108,38:enter,41:enter"
```

It skips the opening, picks the player, *Single Player*, *New Game* and
its *Ok*s; then in each level types `boinga` and, every half minute,
`gameover` ([README.md](README.md#its-cheats)), and skips each cut scene
after four seconds.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux, the emulator by hand (`reluva-tool sith host`, changed for `JKM.EXE`) | The CD checks, the opening, the menus and the first level's start ([README.md](README.md#where-it-stands)) |
| November 2026 | US retail, from its CD's image | Linux, `reluva-tool import` with Jedi Knight's CD 2, then `reluva-tool sith host` on the import, its save folder new | Files the same as the install's; 13 music tracks found; on its first start both CD checks passed without a request, the opening, a new game's cut scene, and play in the first level with its music asked for |
| November 2026 | US retail, from its CD's image | Linux, `reluva-tool sith host` | A game saved from the in-game menu, listed and loaded; *Setup*'s settings saved when stopped; `-everyframe -fps 100`, the world moved about twice as often; `-d3d -gpu`, 154 scenes matching; two copies with `-net`, one hosting *Kill The Fool With The Ysalamiri*, the other joining with its own character, each seeing the other in the level |
| November 2026 | US retail, from its CD's image | Linux, `reluva-tool sith host -skim` from *New Game*, in software at the game's own rate | The whole single-player episode in 25 minutes: its 14 levels in order (`s1l1_rebelbase.jkl` to `s5l4_lowersith.jkl`, both halves of Katraasii), each loaded, drawn and played a few seconds; all 23 of the CD's cut scenes started where the game plays them (the opening, each level's opening and closing scenes from inside the level, the finale); the credits run to their end; nothing stopped, no message box, no Windows call without a stand-in |
| November 2026 | US retail, from its CD's image | Linux, `sith host` with a player's `fullsubtitles 1`, `takemeto 12` and `gameover` typed | The subtitles of `S1L1ECS.SAN` and `S5L3ECS.SAN` as the game shows them, against the viewer's: the same pixels below the picture, one line and two |
| November 2026 | US retail, from its CD's image | Linux, `reluva-tool sith movies -all`, `sith page`, and frames from `sith host` | Every cut scene decoded with its sound; the opening's frames against the game's own, the same pixels ([smush.md](../../../../common/video/smush.md#sources-and-checks)); a frame on the Cut scenes page the same as the prototype's, with its subtitle; the first level drawn from the player's start |
| November 2026 | US retail, from its CD's image | Linux, `sith host -d3d -gpu` and `sith page` | A Direct3D scene of the first level captured from the game: its corners' vertex colours the level's red, green and blue times 255; the viewer's Levels page drawing the level so, beside the game's frame |
