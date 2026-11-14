# And Then There Were None: the interface

> **Status:** partial · **Code:** `src/engines/awe/ui/dialogue_box.cpp`, `src/engines/awe/ui/game_text.cpp` (shared), `src/engines/awe/games.h` (`GameFont`, `DialogueBoxLayout`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`); Reluva's dialogue box looked at in the Scenes page · **Confidence:** from the code unless marked

## Overview

Orient Express's interface
([../../motoe/systems/interface.md](../../motoe/systems/interface.md)) is
its own: the train's cars along the top, the scrapbook, the passports and
the fingerprint cards. This game has none of those; its own are an
inventory sack, a journal, and its menus, which phase 4 of
[../PLAN.md](../PLAN.md) brings in. What both games share and this page
compares: the fonts, and the dialogue box.

## How it works

### The fonts

There's no `Text/fonts.txt`. The code adds the six font files in
`Interface/Fonts` to Windows (`AddFontResourceA`, `0x4151f6`) and makes
nine fonts of them by face name, size in points and bold (`0x40eb10`),
kept in the screen object at `+0x4` on:

| | Face | File | Points | Bold |
|---|---|---|---|---|
| 0 | Arial | `arial.ttf` | 12 | yes |
| 1 | Arial Narrow | `arialn.ttf` | 12 | |
| 2 | Industrial | `innswfte.ttf` | 38 | |
| 3 | Arial | `arial.ttf` | 10 | yes |
| 4 | RulyRG | `rulyrrg_.ttf` | 14 | |
| 5 | RulyRG | `rulyrrg_.ttf` | 14 | yes |
| 6 | Arial Narrow | `arialn.ttf` | 10 | |
| 7 | Industrial | `innswfte.ttf` | 18 | |
| 8 | Industrial | `innswfte.ttf` | 24 | |

The face names are the files' own (their `name` tables): `inhswfte.ttf`
is Industrial Heavy and `rulyssc_.ttf` RulySC, neither asked for. A size is
turned into pixels as Orient Express's are (`MulDiv` by the screen's
pixels an inch, over 72: `0x40ecc7`). The dialogue's is font 1 (its
measure, `0x415ed0`, called with 1 by the table loader and the box).
What the others are for isn't read yet.

### The dialogue box

Drawn by `0x434220` each frame, its rows by `0x434bb0`:

- **The windows:** `interface\dialoguewin01.tga` to `03` (made at
  `0x415b2f`), one of them by the choices' rows
  ([dialogue.md](dialogue.md#the-dialogue-manager)): 90, 115 or 141
  pixels high, its top that much above the foot of the 600-line frame.
  `dialoguewin04` and `05` are the bullets, as in Orient Express.
- **A line said:** centred across the 800-pixel frame (`0x434c0a`), and
  down the box: its top at the box's top plus half what its rows leave
  (`0x4343e2`).
- **The choices:** from x 25 and 10 pixels below the box's top (Orient
  Express: 40 and 25), each `rows × row height + 4` below the last
  (`0x434460`), a bullet 14 pixels before it and 5 down (`dialoguewin05`
  for the one under the mouse); the part's choices, the character's
  extras, then the goodbye. A row that would start at y 585 or lower
  isn't drawn (`0x434bc5`). No pictures as choices.
- **The colours** are the code's, not a table's: `0xd2aa6e` for the
  choices and Narracott, `0xf2e7bd` for the choice under the mouse
  (`0x430af6`), `0xc08a6f` and `0x8f9cb0` for the others.

### The cursors

`Interface\cursor*.tga`: the walk, look, take, talk, open, operate,
listen, stairs, keyhole, back and empty ones, and `cursor01`, `02`. Which
shows when isn't compared yet; it's with the scenes' objects, phase 4.

## In Reluva

`GameInfo::fonts` lists a game's fonts when its code makes them, read in
place of `fonts.txt` (`GameFonts::load`); this game's dialogue font is
"dialogue", the rest "font 0" to "font 8" by their place.
`DialogueBoxLayout` holds the box's pictures, where the choices go, where
they stop and the colours; Orient Express's are the defaults. The
Scenes page draws this game's box so (`awe page-shot <game> scenes out.tga
"run:act 01a, talk marston a forcegen" ms:17000`).

Not yet: the inventory, the journal and the menus (phase 4); which
cursor shows when.

## Sources and checks

- **Where the facts come from:** `ac.exe`: the font set-up (`0x4151f6`
  on), the box's draw (`0x434220`, `0x434bb0`), the colours'
  assignments; the font files' `name` tables.
- **How it's checked:** the box drawn by Reluva in the Scenes page, a
  line and the choices, compared with what the code places; not with
  the running game.
