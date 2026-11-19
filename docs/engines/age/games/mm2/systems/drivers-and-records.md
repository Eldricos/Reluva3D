# Midtown Madness 2: drivers, records and race options

> **Status:** done · **Code:** `src/engines/age/ui/game_menus_drivers.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

Drivers, their stats and race records, and changing a race's conditions. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Drivers and records

The main menu's **SELECT DRIVER**: the driver's name and level in its
box (Left/Right goes to another), and every driver in the grid below it.
Each driver has their own level (Amateur or Professional, chosen when
they're created) and their own record
([record-and-unlocking.md](record-and-unlocking.md#the-record-and-unlocking)). The buttons
beside it (`texture/DVRNEW`, `DVRDEL`, `DVRSTS`, `RACE_REC`) open:

- **Create a New Driver** (`jpg/newp_dlg`): the name, AMATEUR or
  PROFESSIONAL (`texture/Checkbox`), CANCEL and DONE. A name already
  taken (whatever its letters' case) gets the game's `jpg/dupp_dlg`:
  "The driver name you entered already exists. Please enter a unique
  driver name."
- **Delete This Driver**: the game's `jpg/delp_dlg`, "Are you sure you
  want to delete this driver?" (YES and NO). The only driver can't be
  deleted: `jpg/lstp_dlg`.
- **Driver's Stats** (`jpg/drec_dlg`): the driver's best place and time
  in each race at each level. Its checks (`texture/dlg_chkb`) choose the
  events and the cities.
- **Race Records** (`jpg/hoff_dlg`): each race's best time at each level
  among all the drivers, and whose it is. Its checks choose amateur
  times, professional times, the events and the cities. The game's
  professional points aren't kept by Reluva, so that check isn't
  available.

Two controls in each of these menus share a name in `widget.csv` (two
`checkbox`es, five and eight `popup_checkbox`es). They're told apart by
their order. The stats' and records' dialogs are 460 high, so they go
at (50, 10) rather than `menu.csv`'s (50, 37).

**Quick Race** (`main_qck`, "Quick Race starts you in the last race you
ran") goes to the garage for the driver's last race, then starts it.

## Changing a race

The Single Race Menu's own boxes hold the race's conditions, as its
file has them. As the game's help says, they can be changed once the
driver has placed in that race: 1st, 2nd or 3rd as an Amateur (a
Professional's place counts too), 1st as a Professional (a Blitz
finished in time counts as 1st). A cruise's can always be changed. Until
then LAPS, OPPONENTS and CHANGE aren't available (U, everything open,
opens them too). Then:

- **LAPS** (a circuit's) and **OPPONENTS** (up to as many as the race
  has paths for): Left/Right.
- **CHANGE** (`texture/race_chg`, 25 x 122 a state, five states): the
  game's **Change Race Options** (`jpg/Renv_dlg`, at `menu.csv`'s 120,
  52). It holds:
  - The time of day and the weather, each a row of the game's icons
    (`texture/RENV_*`) in the picture's box for it, the chosen one lit,
    chosen with Left/Right or a click.
  - The traffic, pedestrian and police densities, as bars on its grid
    (a tenth of traffic or pedestrians a step, a police car a step,
    0-4).
  - **Hide Pedestrians** (`RENV_HIDE`).
  - DONE (on its left) and CANCEL (on its right), as `widget.csv` places
    them.

A race whose conditions are changed shows "Changed: not recorded" and
isn't kept in the record, as the game's records say: "The race
statistics are recorded for races under default conditions only."
Replays keep the conditions they were run in. Next Race goes back to
the next race's own.

Three things here aren't as `widget.csv` has them:

- `race_chg` has no row in it: its place is measured, up the right of
  the time, weather and densities.
- Its icons (`renv_imorn`... at 208-376, 40 and 82) sit on the picture's
  title. The picture has a box for each row instead, so they go in the
  boxes.
- The icons' names (sun, fog, rain, snow) don't match their pictures (a
  sun, a cloud, rain, a sun behind a cloud). They're placed by picture,
  in the order the lightings number the weathers ([tune.md](../formats/tune.md)):
  clear, cloudy, foggy (the sun behind a cloud), rainy.

**LOCATION**: Enter opens the game's **Race Locales** (`jpg/cenv_dlg`,
"Choose race locale"), the cities to choose from. Left/Right still steps
through them.

**A locked car**: GO DRIVE gets the game's `jpg/lock_dlg`, "You cannot
pick a locked vehicle".

In a circuit, Change Race Options' pedestrian density and Hide
Pedestrians aren't available: "there are no pedestrians in a Circuit
Race", the help says, and the circuits' Peds columns are all 0.

## Sources and checks

- **Where the facts come from:** the menus' files, pictures and dialogs (`jpg/newp_dlg` and the like).
- **How it was checked:** `age.unit.game`; by hand.
