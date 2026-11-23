# SimCoaster: the start-up screens and the loading screen

> **Status:** partial · **Code:** `src/engines/tpw/ui/loading_screen.cpp` · **Checked against:** a recording · **Confidence:** seen, not traced unless marked

## Overview

`Data/Init/` holds the pictures the game shows before its 3D engine
does: the Bullfrog splash after the EA GAMES logo ([opening.md](opening.md)),
and the pieces of the loading screen it shows while a park loads. Each
is there for four screen widths (`640`, `800`, `1024`, `1280`), each in
the languages the game was sold in.

## How it works

### The pictures

In `Data/Init/640/`, for the US release:

| File | Size | What it is |
|---|---|---|
| `Splash_American.png` | 640 × 480 | The Bullfrog splash, black on white ([opening.md](opening.md)) |
| `welcome.png` | 640 × 480 | The loading screen's scene: children on a roller coaster car before a Ferris wheel, a share-price graph, a banknote; the EA GAMES logo top right, QSound's bottom left. The same for every language |
| `welcome_american.png` | 384 × 144 | The "SimCoaster" logo, with the scene behind it |
| `Legal_american.png` | 320 × 96 | The legal text in yellow, with the scene behind it: the words of `UITEXT.str`'s string 929 |
| `bull_<language>.png` | 64 × 144 | The Bullfrog and QSound logos, with a scene behind them: for the other languages, none for American |

The other languages' `welcome_` pictures are their releases' logos:
`Welcome_English.png` is "Theme Park Inc." The other folders hold the
same pictures at their widths (`Init/800/`'s `welcome.png` is 800 ×
600, its `welcome_american.png` 480 × 180); `Init/1280/` lacks some.
Names are spelled in mixed case (`Legal_american`, `legal_american`);
the game's files are matched without regard to it.

### The loading screen

From 1:23 to 1:34 in the recording
([research/footage.md](../research/footage.md#the-loading-screen)),
after a world is chosen and before the park appears: the scene, the
logo over its top left corner, the legal text over its bottom right.
Matched against the recording's frame pixel by pixel, `welcome.png` sits
at (0, 0), `welcome_american.png` at (0, 0) and `Legal_american.png` at
(320, 384), the corner: the pieces drawn whole, opaque, over the scene.
Where `bull_<language>.png` goes isn't seen: over the scene's QSound
logo, at the bottom left, perhaps *(guessed)*.

### The screen's size

Which folder the game takes is the screen's width. `Data/Config.sam`
chooses a configuration by the computer's speed, video memory and
memory, and each configuration has a default resolution, one of
`Data/Resolution.sam`'s (2 is 640 × 480, 3 800 × 600, ... 6 1600 ×
1200), unless the options screen sets another. Today's computers meet
configuration 3's least values, and its default is 6, 1600 × 1200, for
which `Init/` has no pictures: what the game does then isn't known.

## In Reluva

`composeLoadingScreen` (`src/engines/tpw/ui/loading_screen.cpp`) puts
the loading screen together from `Init/640/` for the game's language.
The engine shows it after the front end's opening
([front-end.md](front-end.md)), until the player leaves: the rest of
the front end, which comes between in the game, isn't built yet
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#not-yet-as-the-original)).
`reluva-tool tpw loading-screen <game folder> [out.png]` writes it out,
with a checksum, which the game-files test checks
(`tpw.gamedata.loading`). The unit tests (`tpw.unit.ui`) put one
together from pieces built in the test.

## Sources and checks

- **Where the facts come from:** the recording's loading screen; the
  pictures in `Data/Init/`; `Config.sam` and `Resolution.sam`, their
  comments among them.
- **How it was checked:** Reluva's loading screen set beside the
  recording's frame (scaled to 640 × 480): 4.7 out of 255 apart on
  average, against 14.2 for the scene alone; over the logo 4.0 against
  47.9; what is left is the recording's compression.

## Open questions

- Where `bull_<language>.png` goes, and when the game shows it.
- What the game does on a screen larger than its pictures, and which
  size it picks on a computer of today.
- Whether the loading screen shows anything that moves (a bar, a
  message): the recording shows none in its 11 seconds.
