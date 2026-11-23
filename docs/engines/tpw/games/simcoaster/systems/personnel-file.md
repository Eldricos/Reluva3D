# SimCoaster: the Personnel File

> **Status:** partial · **Code:** `src/engines/tpw/ui/personnel_file.cpp` · **Checked against:** a recording · **Confidence:** seen, not traced unless marked

## Overview

After the front end's opening ([front-end.md](front-end.md)), over the
globe, the "SC Personnel File" window: four players' slots on tabs, the
one shown with its player's rank, shares and progress, or a box to name
a new player, and the buttons to go on or to delete a player. From 1:04
to 1:22 in the recording
([research/footage.md](../research/footage.md#the-sc-personnel-file)).

## How it works

### What it shows

For the player shown, on a ruled page:

| Line | Left | Right |
|---|---|---|
| 1 | The rank, `PROMOTIONLEVELS.str`'s string (1 "Assistant Manager") | "Golden Tickets :" and their number |
| 3 | "Shareholding % :" and the share of the company owned, whole per cent | "Share Price :" and "$ " with the price |
| 4 | "Shares Owned :" and their number | "Portfolio Value :" and "$ " with the shares times the price |
| 6 to 8 | Land of Invention, Polar Zone, Arabian Nights (`THEMENAMES.str` 0, 1 and 3), each "{1} of {2} challenges completed" or "Zone not yet opened" | |
| 10 | "Current Objective :" and the mission (`OPERATION.str`), a comma, its world | |

The labels are `UITEXT.str`'s strings 19 to 35 in `GAME8AA`, grey; the
values in `PERSONNELFILESMALL`, a handwriting, blue; the title,
`TITLEMED`, white. Then three buttons, "Enter Game", "Load A Game",
"Delete Player" (`GAME8AA`), below the page "Quit Game" and "Coaster
Design Kit" (`GAME8AA`, white, on blue), and a woman's portrait, the
board's fifth (`mugshot5`): the personnel director, perhaps, as the
fifth of the e-mail senders `Challenges.sam` lists is
`EMAIL_FROM_HR_DIR` *(guessed)*. The
tabs carry the players' names in `GAME7AA`; a slot without a player
says "New Player" (string 20). The recording's first player was named
"Enter name here", string 32's words, which the name box shows below.

The window is drawn from the interface's flat models
([ui-models.md](../formats/ui-models.md)).

### An empty slot

From 1:15: the page shows its labels with no values, and over the
rank's line a name box (`filebox`): "Enter name here" in `GAME7AA`,
light, on a blue band, selected, and a grey tick (`filebutok`'s
disabled state). Typing replaces it, the name white with a blue bar
after it, and the tick turns green; clicking it makes the player
(1:20): their record shows, with "Current Objective : None" (string 36)
and "Load A Game" disabled, its words faint. "Enter Game", "Load A Game"
and "Delete Player" aren't there, and the Coaster Design Kit is grey,
while the slot is empty. The recording's other player, who had played,
showed "Operation Expand!, Land of Invention", the first mission's, and
the tooltip "Resume this game" over "Enter Game" (`UIHELPTEXT.str` 287).

### Deleting a player

"Delete Player" (its tooltip "Delete this player", `UIHELPTEXT.str`
288) dims the Personnel File to about half its brightness and opens a
smaller window (`!window`): string 915, "{0}" (the player's name, its
title, in `TITLEMED`, white), a blank line, then "Are you sure you want
to / delete this player?" (`GAME9AA`, dark blue, centred, its line
break the string's own); a close button (`b_close`) at its top right,
a tick (`b_okay`) and a cross (`mb_cancel`) at its bottom right. The
tick empties the slot (1:15).

### A new player's numbers

Every number comes from the settings:

- the shares, from `levels/Standard.sam`: `ShareInfo.InitialSharesOwned`
  1000 of `ShareInfo.CompanySharesIssued` 30000, 3 % shown, at
  `ShareInfo.InitialSharePrice` 100: "$ 100000";
- which worlds are open, from each world's `global.sam`:
  `ParkInfo.AvailableFromStart` 1 for Land of Invention (`levels/science`)
  only;
- the challenges, 10 a world: `Challenges.sam` has 30, its comment
  saying "First 10 are for NEW SCIENCE"; the other worlds ten each so
  *(inferred)*;
- the rank, 1 "Assistant Manager"; no objective ("None") until the
  first game, then `OPERATION.str`'s first, "Operation Expand!", in
  Land of Invention, as the recording's player who had played shows
  *(inferred)*.

These are the recording's numbers for both its players.

### The screen's size

The recording's words are the fonts' own widths at 640 × 480 (it was
scaled to 960 × 720): "Shareholding % :" is 65 pixels in it, 66 in
`GAME8AA`; "Assistant Manager" 115 in it and in `PERSONNELFILESMALL`;
"SC Personnel File" 113 against `TITLEMED`'s 112. So the game ran at
640 × 480 there.

### Where things are

Measured on the recording's frame at 1:06, at 640 × 480 (x, y from the
top left):

| What | Where |
|---|---|
| The window, with its shadow | (88, 23), 464 × 430 |
| The title | at (124, 30) |
| The tabs | from (137, 71), 94 × 32 each, side by side; the one shown over the page's edge |
| The page | (122, 99) to (528, 380), its own model's place |
| The labels | x 137 and 337; lines at y 134, 167, 184, 213, 229, 245 and 278: about 6 pixels above the ruled lines |
| The values | x 251 and 451; 2 pixels up on the share lines, 1 down on the worlds' and the objective's, level on the rank's |
| The three buttons | y 315, 112 × 26, at x 141, 269 and 397 |
| The blue buttons | y 391, 145 × 32, at x 123 and 382, on plates |
| The portrait | (573, 394), 49 × 46, on a plate at (568, 389) |
| The name box | (228, 112), 197 × 31; its text at (239, 125); its tick at (390, 111), 32 × 32 |
| The delete dialog | (156, 72), 316 × 186; its title at (187, 80); the question's lines at y 149 and 162, centred; the tick at (394, 214) and the cross at (430, 214), 28 × 28; the close button at (430, 74), 25 × 25 |

### What the player can do

Seen in the recording: the first player deleted (1:14 to 1:15); a new
one named in the empty slot (1:15 to 1:20); "Enter Game" with them, then
a world chosen on the globe (1:22; [globe.md](globe.md)).

## In Reluva

`PersonnelFile` (`src/engines/tpw/ui/personnel_file.cpp`) reads its
models, fonts, strings and a new player's record, and draws the window
in each of its states; `choosePlayer` runs it after the camera's path,
over the globe coming up close and turning ([globe.md](globe.md)), the
players read from the game's save folder and written back as they
change ([players.md](players.md)): the mouse lights a button
(`filebuth`), a tab shows its slot; an empty slot's name box takes a
name (as much as fits on a tab, backspace taking off) and its tick, or
Enter, makes the player; "Delete Player" asks, and its tick empties the
slot (Esc, the cross or the close button don't); "Enter Game" goes on to
choosing a world with the player shown, giving them the first mission
if they had none; "Quit Game" leaves. Not yet
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md#not-yet-as-the-original)):
"Load A Game" is always disabled, as there are no saved games; the
Coaster Design Kit does nothing; there are no tooltips.

`reluva-tool tpw personnel-shot <game folder> <out.png> [name
[-delete]]` draws it over the front end's globe: an empty slot, a new
player, or the delete dialog; `reluva-tool tpw front-end-run` runs the
front end headless through it, naming a player "Tester" and clicking
"Enter Game", which the game-files test checks
(`tpw.gamedata.frontend`). The unit tests (`tpw.unit.ui`) check each
control is where it says, and clickable only when its state shows it.

## Sources and checks

- **Where the facts come from:** the recording's frames at 1:04 to
  1:22; `UITEXT.str`, `PROMOTIONLEVELS.str`, `OPERATION.str`,
  `THEMENAMES.str`; `levels/Standard.sam`, the worlds' `global.sam`,
  `Challenges.sam`; the fonts' widths.
- **How it was checked:** `personnel-shot` set beside the recording's
  frames at 640 × 480 (1:06, 1:14.6, 1:16.1, 1:21): the page's ruled
  lines within a pixel, the rows of words within a pixel or two, the
  buttons, tabs, name box and dialog where the recording has them.

## Open questions

- Where the game keeps its players ([players.md](players.md)), and what
  a played player's file shows (golden tickets, promotions, the share
  price as it moves).
- What "Load A Game" shows; the Coaster Design Kit.
- When the portrait shows: it's there at 1:06 and gone from 1:14.
- How long a name can be.
- Why the values' rows sit differently against their labels; the blue
  buttons' shade ([ui-models.md](../formats/ui-models.md#open-questions)).
- Whether the game says what world a challenge is in, rather than
  counting ten a world.
