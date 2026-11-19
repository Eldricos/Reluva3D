# Star Trek: Deep Space Nine - Harbinger: known differences from the original

Where this engine knowingly behaves differently from DEEP.EXE, and why.
Everything else is meant to match the original; a difference not listed here
is a bug.

## By choice

### Added conveniences (turned off by *Play exactly as the original*)

| Difference | Original | Where |
|---|---|---|
| The game menu appears on the very first launch | The first run starts straight into the game | `game/game.cpp` (`run`) |
| Esc opens the game menu and resumes from it | Right click only | `game/game.cpp`, `game/game_menu.cpp` |
| Confirmations take Y / N / Enter / Esc and show a hint line | Mouse only (right button yes, left button no) | `game/game_menu.cpp` |
| M also closes the deck map | Mouse only | `game/game.cpp` |
| The menu keeps its own palette (setup.pal); the selected save's picture is recoloured to it | The whole menu switches to the selected save's palette, so the menu art changes colour from save to save | `game/game_menu.cpp` (`drawMenuScreen`) |

The recoloured save picture has to make do with the menu palette's colours, so
some pictures may look banded.

### Deliberate simplifications (always on)

* **No disc prompts.** `CD` (require disc) only records the disc number; files
  are looked up on every part of the game's folder (both CDs side by side,
  or Reluva's import of both). Without disc 2, its content is simply missing
  instead of being asked for.
* **Lip-sync redraws.** While a talker speaks, the original renders only when the
  mouth changes shape (`render(dl=2)`), and other animations advance with speed
  step 3 meanwhile. This engine keeps rendering every 20 ms with speed step 3
  and applies the `dl=2` erase rule (stopped `clear` animations are erased too)
  for as long as the line plays. Mouths look the same; other animations may
  differ slightly in timing during speech.
* **New saves' names.** A new save takes the first unused name
  `00000000.ds9`, `00000001.ds9` and so on. The original's counter (0x213F4)
  rolls its digits over oddly, going from `00000099.ds9` to `00001000.ds9`;
  both read any eight-digit name, so saves move between them unchanged.
* **The saves' order in the menu.** By name; the original lists them in the
  order the folder gives them (DOS directory order, 0x218C6).

## Bugs fixed

* **GOSUB stack in saved games.** The original's loader reads 2 bytes per
  return point and then overwrites the value with fread's result, so a game
  saved inside a subroutine call is restored wrongly. This engine reads the
  u32 values the original writes (`game/game_save.cpp`, `loadGame`).
* **DONEX and EMPTYX in saved games and across scenes.** The original's loader
  (`Load_DoneXblock`, 0x20249) writes these dialogue redirections into an
  empty slot and never stores their excluded reply, so they're lost on every
  load, and on every scene change (when the dialogues are written out and read
  back), until the dialogue sets them again *(inferred: the loss on a scene
  change)*. This engine keeps them
  (`game/game_save.cpp`, and the dialogues stay in memory).
* **Out-of-range talking heads.** QUARK020 gives its talker the animation
  4095, meaning none; the original would use it as an index past the end of
  the scene's animations while Quark speaks *(inferred)*. Here a value outside the scene's
  list means no animation (`game/game_dialogue.cpp`, `registerTalker`).
* **Animation priorities in saved games.** A priority above 4 read from a save
  is lowered to 4. The original's renderer only draws levels 0..4, so such an
  animation would never be drawn or advanced there, and a script waiting for it
  would hang. Saves the original writes never contain one.

## Not yet as the original

None yet.

## Additions outside the game

* Reluva's settings ([README.md](README.md#settings)) set the master, music,
  speech and effects volumes (0–200 % of the original's levels) and the
  display (full screen, window size, whole-multiple scaling, smoothing,
  vsync). The game's own menus are unchanged.
* Alt+Enter switches between a window and full screen; Ctrl+F5 goes back to
  the launcher.
* The developer settings (a start scene, the debug keys F2 and F10 and
  the detailed title bar, console output) are off unless set
  ([README.md](README.md#developer-settings)). The scene viewer and the
  one-video mode are in `reluva-viewer`, not the game
  ([README.md](README.md#the-scene-viewer)).
* Saved games and the original's settings file live in the game's own
  folder of Reluva's (`games/<entry>/savegame/` and `games/<entry>/dlas.ds9`)
  instead of the game's install folder. The file format is the original's.
