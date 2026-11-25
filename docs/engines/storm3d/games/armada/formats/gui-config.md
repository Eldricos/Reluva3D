# Star Trek: Armada: the in-game interface's layout (`misc/gui_*.cfg`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/gui_config.cpp` · **Checked on:** the English retail 1.0 install's four races' files, the original's draws of its interface · **Confidence:** layout confirmed; most names' meanings inferred from the draws

## Overview

`misc\gui_fed.cfg`, `gui_kli.cfg`, `gui_rom.cfg` and `gui_bor.cfg`
lay out the interface in play for each race, and include
`gui_glob.cfg` for what the races share: where each panel is on the
screen, which sprites draw it, and where its parts go. The sprites are
the race's table's (`Sprites\gui_federation.spr` and the others) and
`gui_global.spr`'s ([spr.md](spr.md)).

## Layout

Text, a setting a line, CR LF at their ends:

```text
// Include non race specific interface configuration data...
#include "gui_glob.cfg"

speedPanelArea			=			146	327	247	40
speedBackgroundPanel		=	"speed_panel"
speedBackgroundPanelSize	=	2
speedBackgroundPanel_0		=			0	0	128	40
```

- `name = values`: numbers, or names in quotes, split by tabs or
  spaces. `//` starts a comment.
- `#include "file"` reads another, from the same folder.
- An area is four numbers: x, y, width and height, in the screen's
  pixels at 640 × 480; within a panel, from the panel's corner.

The names come in families, by the panel they start with (`speed`,
`officer`, `crew`, `dilithium`, `minimap`, `info`, `control`,
`cinematic`), from the original's draws of a frame of play:

- `<panel>PanelArea`: the panel's place on the screen.
- `<panel>BackgroundPanel` names a sprite, and `<panel>BackgroundPanel_<i>`
  (`<panel>BackgroundPanelSize` of them) the places of its pieces: piece
  *i* is the sprite `<name>.<i>`. Each piece is drawn from its sprite's
  rectangle of a 256 × 256 texture, twice the piece's size, a texel
  sampled at each pixel's top left (point sampling, the texel at twice
  the pixel's place), where its alpha passes the test.
- `<panel>BlackArea`: drawn black, under what the panel shows.
- `<counter>Icon`, `<counter>IconArea`, `<counter>TextArea`: the
  counters' icons, drawn in (229, 229, 229), and the numbers, in the
  sprite font in (128, 128, 128), centred as if each character were 6
  wide, as "3000", "3569" and "4/120" all fall *(inferred)*.
- `minimapDisplayArea` with `minimapBackgroundSprite`,
  `minimapFogSprite`, `minimapShroudSprite`: the minimap's layers, a
  texel a cell of the map's shroud.
- `cinematicDefaultBackground`, `cinematicBackgroundArea`, and the
  view's two buttons, `cinematicCommButton` and `cinematicMenuButton`,
  each over its border, in (153, 178, 171) and (128, 152, 149).

Others (the info panel's single ship and groups, the control panel's
twelve buttons, the comm screen in `gui_glob.cfg`) wait for what fills
them.

## In Reluva

`GuiConfig` (`src/engines/storm3d/formats/gui_config.h`) reads a file
and what it includes, keeping each name's last values. The interface
in play (`Hud`, `play/hud.h`) draws from it
([systems/play.md](../systems/play.md#the-interface)).

## Sources and checks

- **Where the facts come from:** the files; the original's draws of a
  skirmish's frame of play (`host -draws-ms`), each quad's place on the
  screen and its texture's rectangle.
- **How it was checked:** the unit tests (`storm3d.unit.play`) read a
  layout written as the install's is, with an include. Reluva's
  interface beside the original's frame: its panels the same but for a
  few pixels, the counters to the pixel.

## Open questions

- What the names this page doesn't list are for, until what uses them
  is seen.
- How the counters' numbers are really measured.
