# SimCoaster: options

> **Status:** partial · **Code:** `src/engines/tpw/options.cpp`, `src/engines/tpw/ui/info_windows.cpp` · **Checked against:** the settings and string tables · **Confidence:** guessed unless marked

## Overview

"Options" in the game's menu (`UITEXT` 770) opens the Options window
(783), its three tabs Graphics, Sound and Game (784 to 786; "Show
graphics options" and the others, `UIHELPTEXT` 72 to 74). The recording
opens none of it. Its strings give its settings:

| Tab | Settings |
|---|---|
| Graphics | Rendering: hardware 3D card or software, the video card (787 to 792); Resolution, 512 × 384 to 1600 × 1200 (793 to 799); Graphics Quality, Low, Medium, High (800 to 803) |
| Sound | Sound Effects, Speech, Music, each a Volume (804 to 810); Audio Quality (807, 811) |
| Game | General (812): Advisor, Tooltips, Confirmations, Animate Menus, Auto-rotate view when building (813 to 817); Game Speed (818); Right Mouse Button Cancels (819); Camera Rotation, Smooth or 90 degree steps (820 to 822); Camera Scrolling, by the right mouse button or by pushing (823 to 825) |

## How it works

`Config.sam` picks a default config by the computer's speed, memory and
video memory (`Config[n]`, "auto-config"), each with its
`DefaultResolution`, `DefaultGameSpeed` ("Percentage game speed at
start": 50, 70 and up), `PlayMusic` and which of `Low.sam`, `Med.sam`
and `Hi.sam` to use; without a match, `Defaults.DefaultConfig`, 1, whose
speed is 70 *(confirmed)*. `sound.sam` gives the sounds' levels
(`DefaultVolume.SFX`, `SPEECH`, `MUSIC`).

What Reluva follows:

- **Game Speed**, 10 to 100 percent in tens: the park's clock runs at
  its speed over 70, the default config's, which is the pace Reluva's
  clock was set to by the recording ([park.md](park.md)).
- **The volumes**, 0 to 100 percent in tens, for the sound effects,
  speech and music, from `sound.sam`'s levels.
- **Advisor**: off, he says nothing more ([advisor.md](advisor.md)).

The rest show as Reluva has them, grey: software rendering, 640 × 480,
the high detail textures; the other general settings unticked; smooth
camera rotation and push scrolling. The window is laid out by guess,
its boxes the interface's (`i_boxtick`: empty, ticked). The options
are kept in `options.txt` in the game's save folder, the player's own
for every player, and take effect at once.

## In Reluva

`GameOptions`, `defaultOptions`, `readOptions`, `writeOptions` and
`applyOptions` (`src/engines/tpw/options.cpp`); `OptionsWindow`; the
park's clock in `playPark` at the speed. `reluva-tool tpw window-shot
<game folder> <world folder> <out.png> options` (and `options-sound`,
`options-game`) draws a tab. The unit tests (`tpw.unit.ui`,
`tpw.unit.park`) change the settings and keep them.

## Sources and checks

- **Where the facts come from:** `Config.sam` and its comments,
  `sound.sam`, `UITEXT.str`, `UIHELPTEXT.str`.
- **How it was checked:** `window-shot options` read over.

## Open questions

- How the game's speed maps to its clock; whether 70 is the
  recording's.
- The window's look; whether a player's options are their own.
