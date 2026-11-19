# Midtown Madness 2: the player's record and unlocking

> **Status:** done · **Code:** `src/engines/age/game/record.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

What each driver has done, and what it unlocks. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## The record and unlocking

Reluva keeps what each driver has done in a record of their own in the
game's save folder. A record holds:

- each race's best place and best time at each level (a Blitz finished
  in time counts as first);
- the lessons passed;
- the last race run (the main menu's Quick Race starts it again).

The drivers are listed in `drivers.txt` (each one's record file, level
and name). The first driver keeps `record.txt`, the record from before
there were drivers; each new one gets a `record-<n>.txt`. These files
are Reluva's own; the game's driver files (`.plr`) aren't read or
written. The drivers are chosen, created and deleted on the main menu
([drivers-and-records.md](drivers-and-records.md#drivers-and-records)).

- **Races** open three at a time (the `.cinfo`'s UnlockGroup), each
  group once every race before it is placed at its MustPlace (3rd) or
  better at that level (a Professional result counts for Amateur too).
  That's Reluva's reading of the two keys.
- **Lessons** are all open ("You can select lessons in any order", the
  game's help) but the Final, which opens once the three Midterms
  (lessons 3, 7 and 11) are passed: "You've passed all three Midterms
  and it's time for the Final Exam" (its description).
- **Cars and paint jobs**: the game's pictures say what opens each
  (`jpg/<car>_lck.jpg` for a car, `_lck<n>` for paint job n, with `_p`
  for a Professional's), and Reluva follows them: "To unlock this
  vehicle, place 1st, 2nd, or 3rd in six San Francisco Checkpoint
  Races" (Amateur), "... finish first in six ..." (Professional),
  "... pass London Crash Course Midterm Exam Two", "... successfully
  complete the San Francisco Crash Course" (its final lesson). Eight
  cars have them (the Audi TT, the new Mini Cooper, the Light Tactical
  Vehicle, the DB7, the New Beetle Dune, the Panoz GTR-1, the fire truck
  and the New Beetle RSi) and twelve paint jobs (one each of the Audi
  TT, the Mustang Fastback, the London cab, the new and the classic Mini,
  the double-decker, the Dune, the GTR-1 and the fire truck, three of
  the RSi). Every other car and paint job is open. The `.info` files'
  UnlockScore and UnlockFlags (set on six cars) aren't
  used: the pictures cover more.
- `race/<city>/<city>_rewards.csv` says the same in other words: the race
  kind (`blitz`, `circuit`, `race` for Checkpoint, `crash`), `half` or
  `all` of them (a lesson's number for `crash`), the car, its paint job
  (0: the car) and the results screen's message. All twenty rows agree
  with the pictures (half is the five Blitz or Circuit races of ten, six
  Checkpoint races of twelve), but for one number: the fire truck's
  paint job is 2 there and `vpsemi_lck4` in the pictures. So a reward is
  matched to its rule by car, city and kind; when a result opens it, its
  message shows on the results screen.
- **U** in the menus opens everything, for looking round.

## Sources and checks

- **Where the facts come from:** the `.cinfo` race groups, the menus' locked pictures; the unlocking seen in the 100% runs of public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.game`.
