# Midtown Madness 2: boats and drawbridges

> **Status:** done · **Code:** `src/engines/age/game/gizmos.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The boats, ducks and ferries on their paths, and the drawbridges. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Boats and drawbridges

`Gizmos` (`gizmos.cpp`) moves the things on the cities' path sets
([pathset.md](../formats/pathset.md)): 17 boats in San Francisco (a
car ferry, sailboards, sailboats, ducks) and 22 in London (car ferries,
tugs, water taxis, ducks), and London's four drawbridges and San
Francisco's Chinatown gate, which stands still.

- **Boats** go round their paths or back and forth, at the path's speed
  as Reluva reads it, facing the way the path goes over a stretch either
  side (40 m for a ferry, 12 for tugs and taxis, 3 for others, half a
  metre for ducks, so long boats turn as a whole), bobbing 8 cm (2 for
  ducks). They don't collide.
- **Drawbridges**: each leaf turns up about its hinge, the end at the
  bank. They go up and down on a timer: 74 seconds down, 8 rising, 40 up
  (to half a radian, about 29°), 8 falling, 130 s in all, each bridge
  29 seconds behind the one before; a race's `open:` bridge starts up.
  The 130 s fits two runs of London Run in the footage
  ([footage.md](../research/footage.md)): raised 10-18 s into the race,
  down at about 80 s, raised again at about 140 s. The phases' lengths
  are Reluva's within that. A leaf is as wide as half
  its model, which is modelled about the middle of its length with its
  road at its origin's height, and is drawn hinged at the path's height.
- **Driving over them**: a leaf's road is two triangles that move with
  it, checked with the city's. The city's own road reaches across the
  gap (untextured triangles under Tower Bridge, at the bridge's height);
  those whose middle is under a leaf, within 1.5 m of its height, are
  taken out of the drawing and the collisions, so a raised bridge leaves
  a gap. Down, a car crosses level (7.9-8.1 m on Tower Bridge); up, it
  jumps the gap or drops into the river.
- **Traffic** stops at a bridge that isn't down: a line of road users
  across each hinge, every 2 m, that cars held up 10 seconds don't drive
  through as they do through others.

No ships were seen near a raised bridge in the footage, so the timer
isn't tied to boats. Tower Bridge was down when crossed in Bridge Bash.

## Sources and checks

- **Where the facts come from:** the path sets ([pathset.md](../formats/pathset.md)); the bridges looked at in public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.city`; by hand on the World and Drive pages.
