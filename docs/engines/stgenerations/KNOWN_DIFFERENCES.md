# Star Trek: Generations: known differences from the original

Where this engine knowingly behaves differently from `sgens.exe`, and why.
Everything else is meant to match the original; a difference not listed
here is a bug. (Where the engine differs from STG-Engine, which it came
from, see [PORTING.md](PORTING.md).)

## By choice

| Difference | Original | Where |
|---|---|---|
| The captain's menu comes after the title, before a game starts, so a saved game can be loaded | The title goes straight into a new game | `game/frontend.cpp` (`startFrontEnd`) |
| 1-9 use the item in that inventory slot (as well as the inventory, clicked) | The inventory only | `game/generations.cpp` (`handleWalkKey`) |
| The 3D view is drawn with a depth buffer | Sorted spans: the same picture for the game's levels | `game/render3d.h` |
| Stored files in the archives whose CRC doesn't match are used as they are, with a warning (a copy may have a few bits wrong in its music) | Not checked | `formats/resources.cpp` (`open`) |
| A save in an away mission keeps the moment: loading carries on from where it was saved. Space combat saves as the original does, and loading starts the fight again from its briefing | Saves only where the story is ("Save game at start of current mission"): loading starts the mission again | `game/generations.cpp` (`saveGameState`), `game/campaign.cpp` (`restartSection`) |
| Escape gives up choosing a key on the Controls screen, so it can't be bound | Takes the next key whatever it is, Escape included (shown as `?!?`) | `game/optionsmenu.cpp` (`keyDown`) |
| Keys the key map leaves free keep this engine's own uses: WASD, End, X, C, Space, B, H, O, `[`, `]` and 1-9 in missions; Q, E, F, G, = and -, Tab, T and V in space combat. A key bound on the Controls screen always does its bound action instead | Loading the key map replaces the game's whole built-in key table, so only the keys on the Controls screen do anything | `game/generations.cpp` (`handleWalkKey`), `game/combat.cpp` (`keyDown`) |
| Mouse Look's speed is scaled by the same ratio as the turn and look keys', so the two stay in step | Turns the view a fifth of the way to the mouse's heading each frame | `game/generations.cpp` (`mouseLookRates`) |

## Bugs fixed

None yet.

## Not yet as the original

| Difference | Original | Where |
|---|---|---|
| No small window: the view is always 640 wide, so Settings shows the window size dimmed at LARGE, and a click on it gets the menu's refusal sound | The small window's view is 504 wide, within the same 640 x 480 screen | `game/optionsmenu.cpp` |
| Arriving waves (SC9 only) appear at once ([UNCONFIRMED.md](UNCONFIRMED.md)) | They may fade in as they uncloak | `game/combat.cpp` |
| Saved games are STG-Engine's, not the original's: a game saved by `sgens.exe` can't be loaded | The original's own format | `game/generations.cpp` (`saveGameState`, `loadGameState`) |

## Additions outside the game

- Reluva's settings ([README.md](README.md#settings)): the music, effects
  and speech volumes, the difficulty and subtitles to start with (the
  game's menu still sets them), the master volume and the display (full
  screen, window size, whole-multiple scaling, smoothing, vsync).
- The game's pointer is the system's pointer, scaled with the picture.
- Alt+Enter switches between a window and full screen; Ctrl+F5 goes back
  to the launcher. The game pauses while its window isn't in front.
- The developer settings (the resource viewer, console output, a section
  of the story or a space combat to start in, the CDs' folders) are off
  unless set ([README.md](README.md#developer-settings)).
- The saves and the menu's settings live in the game's own folder of
  Reluva's (`games/<entry>/`) instead of the game's install folder.
- No CD is needed in a drive, and none is swapped: both CDs' files are
  used at once, from an import or the folders given.
