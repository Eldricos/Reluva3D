# Midtown Madness 2: Cruise and the stunt

> **Status:** done · **Code:** `src/engines/age/game/race_cruise.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

The race menu's Cruise, and San Francisco's stunt mode from its unused files. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

The race menu's **Cruise** (`cruise`, the first of its events) is the
city with no race: no checkpoints, no clock, no countdown. "Explore the
city, become one with your vehicle, and practice up for some serious
racing, without the pressure of the clock", as the game's help puts it.

- Its conditions are the menu's (Change Race Options), always open to
  change. They start from morning, sunny, the traffic its `roam.aimap`'s
  `[Density]` gives (London's 0.1; San Francisco's has none, so the
  same: [traffic.md](traffic.md#how-many)), half the
  pedestrians and 4 police.
- Its police patrol from `race/<city>/roam.aimap` (`.aimap_p` at
  Professional, with one more in San Francisco): 19 and 20 of them in
  San Francisco and London, at places across the city. That's Reluva's
  reading of the name ("roam"); the game's own cruise isn't otherwise
  described by its files.
- It starts at one of the city's Cops & Robbers places
  (`multicopwaypoints.csv`, as a multiplayer cruise does), picked by the
  car, so its replay starts in the same place.
- Busted or wrecked, the car is put back where it started, mended.
- It plays one of the cruising tunes, as the Drive page does.
- Esc pauses; Quit to Race Menu ends it. Nothing is recorded but the
  last race run (Quick Race).

**Stunt Mode: Acceleration.** San Francisco has one more file no race
names, `race/sf/accel0waypoints.csv`, and `mmlang.dll` has a group of
messages no lesson uses (199-204): "Stunt Mode: Acceleration", "Race to
the end of the pier!", "But don't fall in!", "Go!", "Put the pedal to
the metal!", "Great job! You can stop on a dime!". Reluva puts them
together as the game's stunt, offered as Cruise's second race in San
Francisco:

- The file's five points run 1.5 km in a straight line down the hill
  to the bay (Market Street, it looks like), from 40 m up to the water's
  level, with road under all of it. Measured by casting down onto the
  city's collision every 40 m: the ground runs on some 140 m past the
  last point, then stops. That's the pier's end.
- The car starts at the first point facing the second (the file's
  first heading, 39.6 degrees, points 25 degrees off the road, into the
  buildings).
- The messages count it down (200, 201, 202), then "Put the pedal to
  the metal!".
- The points after the first are checkpoints, as in a race. Past the
  last, stopped (under 0.5 m/s) is "Great job! You can stop on a
  dime!", with the time and how far short of the pier's end. Falling in
  (3 m below the lowest point) fails, as do being busted or wrecked.
- Traffic is the cruise's (0.1) and there are no police.

**Stunt Prototype.** `race/sf/stunt0waypoints.csv` (one point, a start)
and `stunt0.aimap` (traffic 0.13, three roads without it, no police, an
opponent `vpford opp-c0.2` whose path isn't there) look like the "Stunt
Prototype" of the game's mode names (`mmlang.dll` 526, beside
"Testing"). Reluva offers it as Cruise's third in San Francisco: a
cruise from that start, with that traffic, without the opponent.

The `jump0`-`jump2` points and `ramp.pathset` aren't used (see the
Crash Course's columns below).

## In Reluva

`RaceScreen` runs Cruise and the stunts
(`src/engines/age/game/race_cruise.cpp`). The stunt mode and the Stunt
Prototype are Reluva's additions, put together from files no race names
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

## Sources and checks

- **Where the facts come from:** `roam.aimap`, `accel0waypoints.csv` and `mmlang.dll`'s messages; the game's help file's words.
- **How it was checked:** `age.unit.game`; by hand.
