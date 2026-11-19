# And Then There Were None: the inventory

> **Status:** done · **Code:** `src/engines/awe/attwn/inventory.cpp` (this game's), `src/engines/awe/attwn/overlay.cpp` (it with the journal), `src/engines/awe/games.h` (`InventoryRules`) · **Checked against:** the code (`ac.exe`); Reluva's inventory looked at in the Scenes page · **Confidence:** from the code unless marked

## Overview

The inventory is a panel at the left edge of the screen: only its sack
shows until the sack is clicked, or the right button or I pressed, and it
slides out. It holds 12 items a page and a tray of four below them, where
items are put to be examined or combined. Orient Express's is a bar along
the foot of the screen, 16 items a page, with cards for passports and
prints ([../../motoe/systems/interface.md](../../motoe/systems/interface.md#the-inventory-window));
this game has no cards.

## How it works

### The tables

`Text\itemlist.txt` (`0x41b220`): by its Item Name (column 2), an item's
description (column 7) and Examine (column 8: `0`, `docs <n>` or
`books <n>`). `Text\uselist.txt`: the parts, the result, whether it can
be taken apart and put together, a condition and an event, as Orient
Express's. Using an item on a character is `Text\charlist.txt`'s, where
Orient Express has `uselist.txt`'s characters; using one that doesn't go
says `talk narracottgen 2`.

### The panel

`interface\inventoryborder.tga` at y 0: at x -341 closed, -8 open
(`0x417fe0`); it slides at 500 pixels a second, a whole pixel at a time,
with `sound\interface\openclose.wav` (`0x417e20`). The sack,
`interface\inventorysack.tga`, at its x + 342, y 12; a click from x + 339
to 382, y 8 to 54 opens or shuts it. While it slides it takes every click
and does nothing with them.

- **The grid** (`0x418860`): 4 by 3 boxes, 71 apart, from the panel's
  x + 34, y 47; a box takes clicks 64 square; an item's picture,
  `inventory\<item>\inv<item>.tga`, 5 in. The tray is a row of four more
  at y 260, slots 144 to 147.
- **The pages**: as many as the highest page holding an item and one
  more, at most 12, never fewer than before (`0x418660`). A blip at
  x + 69 + page × 192 / (pages − 1), y 29 says which; the arrows (x + 50
  to 78 and 283 to 311, y 28 to 43) turn them, round from either end,
  with `click1.wav`.
- **The description**: of the item under the mouse, with nothing in hand
  (`0x418290`), in font 6, `0xffd2aa6e`, centred in 265 pixels from
  x + 41, at y 378 (`0x4187a3`).
- **A new item** (`0x418cfd`): its page shows and the sack blinks for 5
  seconds, hidden every other third of a second while the panel's shut;
  then, or on the panel's closing or an item picked up, it's in the bag
  (`0x417f50`).

### Picking up and putting down

A click on an item (`0x418fe0`) takes it in hand, its cursor
`inventory\<item>\cur<item>.tga`; on an empty box, puts the one in hand
there; both with `click4.wav`. In hand on the scene it's used as Orient
Express's are (`0x419380`, `0x4194f0`). Taking an item says
`sound interface\inventory` (`0x40d54e`).

- **The cursor** (`0x416180`) with an item in hand: `cursoroperate` over
  something it can be used on (`0x419300`: the object's own use, or for a
  character `charlist.txt`'s, both only asked), else `cursorempty` with
  the item's cursor picture 22 across and 23 down from it (`0x4162ce`),
  kept 62 pixels in from the right and the bottom.
- **Escape** (`0x416f82`) puts the item in hand back in the bag
  (`0x4192e0`), a new item in the bag, and the close-up away without
  Narracott's word on it (`0x41a010` given 0), before the in-game menu.

### Taking an item

`take <item>` and `take this` (`0x40d5be`, `0x40d590`) add the item new,
state 1 (the runner's `ebp`, `0x40b57f`): its page shown and the sack
blinking (`0x418ce6`). Loading a saved game adds each item the same way,
with the state, slot and page saved (`0x413a30`).

### The characters' table

`Text\charlist.txt` isn't read as a table (`0x41a420`): its first line
is passed over, and any shorter than 20 characters; the rest are cut at
tabs and line ends, a run of them as one (`strtok` on `"\t\n"`), into
the item, the character, the condition and the event, each 256
characters; a cell's opening quote is dropped, and its closing one with
it. A row is used (`0x4194f0`) when the item in hand is its item,
ignoring case, the character's model has `<character>_model.gr2` in its
name (`brentknit` counting as `brent`), and its condition holds. The
install's 17 rows read the same as a table's would.

### Examine

The button at x + 137 to 179, y 328 to 370 (`0x419790`): the item in
hand goes to the tray first; with one item there, or two prints:

- one with `exm<item>.bik` or `.tga`: `talk narracottgen 41, examine`;
  two prints of one person (`printbrent`, `printmarston`,
  `printrogers` in both names): `examine, talk narracottgen <59, 153,
  56>, tag compare_<who>` (`0x419919`);
- else by its Examine (`0x419a11`): `docs <n>` sets `read_<item>` and
  says `sounda interface\notebook, wait .5, talk narracottgen <n>,
  journal 2`; `books <n>` the same with `journal 3`, or `talk
  narracottgen 183` once read; anything else `talk narracottgen 38`.

`examine` (`0x419bd0`) shows the close-up centred on the screen, at
((800 − its width) / 2, (600 − its height) / 2) (`0x419c7c`): the movie,
round and round, or the picture; two prints 50 either side of the
middle on `interface\fingerprintpaper.tga`. A click puts it away
(`0x41a010`): after a movie Narracott says `talk narracottgen 184`; the
item in hand goes back; `journal 2` uses the document up.

### Combine

The button at x + 185 to 225, y 328 to 370 (`0x41a740`): the item in hand
to the tray first; prints go to Examine; one item is taken apart
(`0x41a8f0`) by a row whose result it is and that can be, its parts put
in the tray, `sound interface\decombine` and the event; such a row that
can't be says `talk narracottgen 39`, none `40`. Two or more are put
together (`0x41ab30`) by a row whose parts are all of the tray's, the
parts used and the results in the tray, `sound interface\combine` and the
event, or `talk narracottgen 4` without one; none says `3`.

### In a saved game

`ITEM`'s states are 1 new, 3 in the bag, 4 in hand, 5 used, where Orient
Express's are 1, 2, 3, 4: the code sets 3 putting an item in the bag
(`0x417f5c`, `0x4192e7`), 4 taking it in hand (`0x419231`), 5 using it
up (`0x419660`), and never 2. The loader gives each its state, slot and
page as saved (`0x413a30`); a page is the panel's 12.

## In Reluva

`attwn::Inventory`, with the journal in one `GameOverlay`
(`attwn::Windows`, `attwn/overlay.cpp`): the shared scene page gives it
clicks, right clicks, keys and the mouse first, and `examine`. The items
are the shared story's, in Orient Express's states. `InventoryRules` in
the per-game table has what the shared code does otherwise: no cards,
the cursors' path, the characters' table, what not going says, the
taking sound, the save's states and the page's size, where the item sits
by the cursor, and how the characters' table is read
(`attwn::readCharacters`). The `sce<item>` pictures in `Inventory\` are
the items lying about: the scenes' own pictures (74 of them named by the
scenes), drawn as any, their `_night` ones at night
([lighting.md](lighting.md#the-nights-own-pictures)).

## Sources and checks

- **Where the facts come from:** `ac.exe`'s inventory object (`0x4178a0`
  to `0x41b300`): its update, draw, clicks, the Examine and Combine
  buttons and `examine`; the constants it places by; `itemlist.txt` and
  `uselist.txt` as installed.
- **How it's checked:** `awe.unit.attwn` blinks a new item, opens the
  panel, turns pages, picks up, puts down in the tray, combines, takes
  apart and reads a document into the journal. The panel was looked at in
  the Scenes page in 1B with three items, one described (`awe page-shot
  <game> scenes out.tga start:1b "run:take record, take rogersletter,
  take bookbeekeep" ms:6000 rclick:400,300 ms:1500 move:75,85`).
