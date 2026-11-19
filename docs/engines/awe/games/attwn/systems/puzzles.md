# And Then There Were None: the puzzle screens

> **Status:** done · **Code:** `src/engines/awe/attwn/puzzles.cpp` (the bookcase, the deposit boxes), `src/engines/awe/motoe/puzzles.cpp` (the safe, shared), `src/engines/awe/games.h` (`SafeRules`) · **Checked against:** the code (`ac.exe`); Reluva's looked at in the Scenes page and solved by `awe game-run` · **Confidence:** from the code unless marked

## Overview

Three scenes are puzzles of the code's own, by their `PUZZ` record: the
library's bookcase, the safe room's safe and its deposit boxes, the way
from the house to the grotto in act 5C. The safe is Orient Express's
([../../motoe/systems/puzzles.md](../../motoe/systems/puzzles.md)) with
other numbers; the other two are this game's.

## How it works

### The puzzle object

As a scene loads, its `PUZZ` value, compared exactly, makes the puzzle
(`0x42da60`): `bookcase` (`0x4081a0`), `depositbox` (`0x408de0`),
`safecombo` (`0x42b020`); any other makes none (`hard`, in 23 scenes, is
something else). It's deleted with the scene (`0x403420`). Like Orient
Express's, it sees a left button press before the hotspot under it runs
(`0x416bb7`), every release (`0x416dc0`), the scene's `action` verb
(`0x40b67e`) and condition (`0x40a2f1`); none of the three draws
anything itself: the scene's pictures are moved, turned, shown and
hidden. A press with an item in hand is a use, and the puzzle doesn't
see it (`0x416ac0`).

### The bookcase (`library03`)

It keeps nothing: the tags `bookshelf1` to `3` are the book on each
shelf, 1 *Navigating the Panama Locks* (`booknavpan`), 2 *Safe Passage
of the South Sea Islands* (`booksafepass`), 3 *Sailing the Florida Keys*
(`booksailflo`), 0 none; unset, the shelf has its first book: 2, 3 and 1
(`0x408270`). The shelves are hotspots: an empty one takes a book in
hand, a full one gives its book.

- **`action takebook<n>`** (`0x4081f0`): `interface\inventory.wav`, the
  shelf's book taken, the tag 0, the scene's conditions tested again.
- **`action placebook<n> <book>`** (`0x4082fe`): the tag set to the
  book's number, the conditions tested again; then if each book is on
  the shelf of its number (`0x408470`), `bookcasesolved` set and
  `sfx\bookcase.wav`; else a book put on its own shelf says `talk
  narracottgen 122`, "I think I heard a click".
- **`action <word>`**, a condition: always holds (`0x408460`).

`bookcasesolved` shows the secret door to the safe room.

### The safe (`saferoom01_dial`)

Orient Express's safe, read the same way (`0x42b1e0`, `0x42b300`,
`0x42b370`), but for:

| | And Then There Were None | Orient Express |
|---|---|---|
| Its `PUZZ` | `safecombo` | `safe` |
| The numbers | 28 left, 11 right, 49 left (`0x42b0ec`) | 6, 6, 6 |
| The dials | `largedial.tga` and `smalldial.tga`, turned together | one |
| Its number, the numbers in and the way | read back from `safecombo_dial`, `_index`, `_dir` each visit (`0x42b020`) and turned to | written, never read |
| Starting again | `talk saferoom 10`; the dial stays | `talk gen_safe 1`; the dial to 0 |
| The third number in | `talk narracottgen 122` | `sound safeclick, talk gen_safe 2` |

The handle's `action open` is the scene's; it sets `safecombodone`.

### The deposit boxes (`saferoom02_depositboxes`)

Twelve drawers, 4 by 3 from (102, 65), 154 and 159 apart, 133 by 139
(`0x4096c0`, `0x4091f0`), each with a letter's picture on it. Their
order is the text tag `depositbox_order`, at first `a e e01 e02 h l r s
t u v w` (`.jpg`), and which are shut `depositbox_visible`, at first all
(`0x408de0`).

- **A press** (`0x408f90`; not once solved, nor while two swap): an open
  drawer is shut (`sfx\boxclose2.wav`); a shut one opened
  (`sfx\boxopen1.wav`), and four give what's in them once, by place, not
  letter: the second the raven earring (`talk narracottgen 124`), the
  fifth the legal document (`125`), the eighth the microphone (`127`), the
  eleventh the "Row your boat" card (`126`). Two open, they swap 0.75
  seconds later.
- **The swap** (`0x408f30`, `0x4097b0`; the time counts only while
  nothing's said): the two letters change places, every drawer shut;
  then if they read RULE THE WAVES, any E in an E's place (`0x4092d0`),
  `depositboxsolved` and `talk narracottgen 92, fade out, scene saferoom02
  4 0, fade in, talk saferoom 11`; else the first time, `talk
  narracottgen 123`, "That swapped the positions of the two drawers."

## In Reluva

The puzzles are ScenePage's, as Orient Express's are: `openPuzzle`
makes them by kind, the hooks pass them the presses, releases, `action`
words and conditions. `SafeRules` in the per-game table has what the
safe does otherwise. For the tools, the safe and the deposit boxes are
solved as a player's mouse would (`puzzleStep`): the deposit boxes'
drawers with something in them opened first, then each place given its
letter by a swap. The bookcase is left to the search: its shelves are
hotspots, its books items.

The arrows' highlights are the scene's pictures `rightover.tga`,
`leftover.tga` and `middleover.tga`, each with the condition `0`: shown
only while the mouse is over the hotspot of that name, as every
hotspot's picture is (`0x414cc0`: the mouse over one that can be
clicked shows the object its `HOTS` name names and plays its hover
sound, and leaving hides it and stops the sound), which the shared scene
page does as for Orient Express.

The numbers are in a coded telegram to Anthony Marston: decoded, the
`decodedmessage` document reads "our agent reported safe in house with
combination L28 R11 L49" (`Text\Journal\docs.txt`), and the journal's
General tab keeps "the combination of the safe - L28, R11, L49" once
it's held.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s puzzle objects at the
  addresses above, and the scenes' own scripts.
- **How it's checked:** `awe game-run` with `solve` in act 5C: the
  deposit boxes give their four things and open the door; the safe takes
  28 11 49 and opens. The deposit boxes were looked at in the Scenes page
  with a drawer opened.
