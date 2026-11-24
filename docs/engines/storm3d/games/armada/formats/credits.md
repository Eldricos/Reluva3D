# Star Trek: Armada: the credits' script (`Credits.txt`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/credits_script.cpp` · **Checked on:** the English retail 1.0 install's `Credits.txt`, the original showing it · **Confidence:** layout confirmed by the frames; meanings inferred

## Overview

`Credits.txt`, in the install's folder, is what the credits show: pages
of names, and commands for how they look. The program opens it as
`credits.txt` when the main menu's Credits is chosen
([systems/front-end.md](../systems/front-end.md#the-credits)).
`Credits2.txt` beside it is the same but for its first colour, (0.4,
0.8, 1.0), and the program doesn't read it.

## Layout

Text, a line each, CR LF at their ends. 778 lines, 57 pages:

```text
@colour=(0.0,0.3,0.6)
@scale=(0.8,0.8,0.8)
@hold=6.0
Star Trek: Armada
is dedicated to
Lee Brisken
A True Survivor
@cls
```

- A line starting `@` is a command:
  - `@cls` ends a page.
  - `@hold=<seconds>` is how long the pages after it stay up.
  - `@scale=(x,y,z)` sizes their letters; the three are always the same.
  - `@colour=(r,g,b)` colours them, 0 to 1. The file sets it once.
- Any other line is a line of the page; a blank one leaves a line's
  room, as the second page's frame shows.
- A command holds until it's set again, for every page after it.

## In Reluva

`CreditsScript` (`src/engines/storm3d/formats/credits_script.h`) reads
the pages, each with the settings in force where it ends, leaving out
commands it doesn't know and pages with no lines. The credits screen
shows them ([systems/front-end.md](../systems/front-end.md#the-credits)).

## Sources and checks

- **Where the facts come from:** the file itself; the original's frames
  of its first pages.
- **How it was checked:** the unit tests (`storm3d.unit.credits`) read
  a script written as the install's is. Reluva's credits put the first
  two pages' lines on the original's rows.

## Open questions

- Whether a page's scale applies across and down alike: the file never
  gives two different numbers.
