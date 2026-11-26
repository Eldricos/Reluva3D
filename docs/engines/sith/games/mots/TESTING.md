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

### The basic engine test

The engine's, `sith.engine`, on Jedi Knight's files.

### The game-files test

Not yet.

### What the automated checks don't cover

Running the game: the CD checks, its cut scenes, levels and music,
saves, the graphics card, multiplayer, by hand with `reluva-tool sith
host`, and the whole episode skimmed with `-skim`
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
