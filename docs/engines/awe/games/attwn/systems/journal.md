# And Then There Were None: the journal

> **Status:** done · **Code:** `src/engines/awe/attwn/journal.cpp` (this game's), `src/engines/awe/ui/game_overlay.h` (the shared page's way to it) · **Checked against:** the code (`ac.exe`); Reluva's journal looked at in the Scenes page · **Confidence:** from the code unless marked

## Overview

Narracott's journal is a panel at the right edge of the screen: only its
edge shows, with a book on it, until the book is clicked and it slides
out. It has four tabs (General, Guests, Docs, Books), each a contents page
of sub-headings and, for one chosen, its entries. An entry shows once its
condition holds. Orient Express has nothing like it (its notebook is the
scrapbook, in the inventory).

## How it works

### The tables

`Text\Journal\general.txt`, `characters.txt`, `docs.txt`, `books.txt`,
one a tab (`0x41c360`): `Sub-Heading`, `Condition`, `Entry`, tab
separated, a header row; a line shorter than 10 characters is skipped,
quotes come off. An entry shows when its condition holds, tested when the
journal opens and on `journal <n>` (`0x41c600`).

### The panel

`interface\journalborder.tga` (386 by 437) at y 0: at x 754, only its left
edge on screen, when closed; at x 420 when open. Clicking the book
(`interface\journalbook.tga`, at the panel's x + 8, y 13; the click
anywhere from x + 3 to x + 46, y 8 to 54) opens or closes it, with
`sound\interface\openclose.wav`; it slides at 500 pixels a second, a
whole pixel at a time (`0x41bf3e`). While it slides it takes every click
and does nothing with them. Open, it takes the clicks on it.

### The pages

All in font 5 for titles and font 4 for the rest (RulyRG 14, bold and not:
[interface.md](interface.md#the-fonts)), colour `0xd0461e00` (a dark
brown, alpha 208).

- **The contents** (`0x41c720`): the tab's name centred in 200 pixels from
  the panel's x + 115, y 40; the sub-headings of the entries showing, in
  table order (one is left out if the last listed contains it), from y
  65, 25 apart, a star (`journalstar.tga`) 15 before each and 5 down; a
  sub-heading that would start at y 315 or lower goes on the next page.
  Clicking one shows its entries.
- **A sub-heading's entries** (`0x41ccb0`, `0x41cff0`): the sub-heading as
  the title, split at a space onto a second line 15 lower when it's wider
  than 230 (the entries then start 10 lower); each showing entry of it
  from x + 110, y 65, drawn as it is if it fits in 230 pixels, else
  wrapped at 230 (two spaces after `.`, `!`, `?`); its rows the text's
  height less 5 apart, half that again after it; in General and Guests
  a bullet (`journalbullet.tga`) 12 before its first row and 8 down; on to
  the next page from y 315.
- **The arrows**: `arrownext.tga` at x + 290, y 335 when there's a page
  after, `arrowlast.tga` at x + 100 when there's one before, and always
  on a sub-heading's entries (`0x41ca80`); each turns a page, with
  `pageflip.wav`, and the back arrow on a sub-heading's first page goes
  back to the contents, to the page it was picked on (`0x41bbbf`). Seen
  so in the footage of Rogers's letter.
- **The tabs**: their boxes at x + 81 to 157, 158 to 224, 225 to 289, 290
  to 351, y 364 to 394 (`0x41b544`), their names (`tab1` to `tab4` in
  `interface.txt`) centred in them, the shown one in font 5. A click on
  one shows its contents, with `pageflip.wav` (the code has
  `pagesflip.wav` for an animation at `+0x14` that's never made).

### `journal <n>`

The tab n (0 to 3) and its contents (`0x41c290`); the entries tested
again; with the journal closed, the book blinks for 5 seconds (hidden
every other third of a second, `0x415c00`), else a page flips. It also
puts the inventory's close-up away (`0x41a010`, [inventory.md](inventory.md#examine)).

## In Reluva

`attwn::Journal`, a `GameOverlay` (`ui/game_overlay.h`): the shared scene
page makes it for the game (`GameInfo::overlay`), updates and draws it in
the place of Orient Express's bar and inventory, gives it clicks first and
asks it of verbs it doesn't know.

Not yet: the journal kept in a saved game (it isn't in the save's records:
which entries show is the conditions').

## Sources and checks

- **Where the facts come from:** `ac.exe`'s journal object (`0x41b400` to
  `0x41d600`): its construction, draw (`0x41bc80`), update (`0x41bf00`),
  clicks (`0x41ba90`), pages and verb; the constants it places by.
- **How it's checked:** `awe.unit.attwn` reads two tables, tests their
  conditions, opens and closes the journal, clicks its tabs and a
  sub-heading. The journal was looked at in the Scenes page in 1B, the
  contents and a letter's entries (`awe page-shot <game> scenes out.tga
  start:1b "run:tag read_rogersletter, journal 2" click:780,30 ms:1500`).
