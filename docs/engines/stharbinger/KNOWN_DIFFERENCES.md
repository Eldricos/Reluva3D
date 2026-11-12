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

## Bugs fixed

* **GOSUB stack in saved games.** The original's loader reads 2 bytes per
  return point and then overwrites the value with fread's result, so a game
  saved inside a subroutine call is restored wrongly. This engine reads the
  u32 values the original writes (`game/game_save.cpp`, `loadGame`).

## Not yet as the original

None yet.

## Additions outside the game

* Reluva's settings ([README.md](README.md#settings)) set the master, music,
  speech and effects volumes (0–200 % of the original's levels) and the
  display (full screen, window size, whole-multiple scaling, smoothing,
  vsync). The game's own menus are unchanged.
* Alt+Enter switches between a window and full screen; Ctrl+F5 goes back to
  the launcher.
* The developer settings (start with the scene viewer or one video, a
  start scene, the debug keys F2 and F10 and the detailed title bar,
  console output) are off unless set
  ([README.md](README.md#developer-settings)).
* Saved games and the original's settings file live in the game's own
  folder of Reluva's (`games/<entry>/savegame/` and `games/<entry>/dlas.ds9`)
  instead of the game's install folder. The file format is the original's.
