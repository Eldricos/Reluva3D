# Midtown Madness 2: pedestrians

> **Status:** done · **Code:** `src/engines/age/game/pedestrians.cpp`, `src/engines/age/render/ped_model.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

The people on the sidewalks, walking and diving out of the way. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

- **Models**: the city aimap's pairs, the second (dressed for rain) when
  the weather is rainy; each in one of its 48 paint jobs.
- **Where**: on the road map's sidewalks (not the sides it says have
  none, kind 3: the Underground, ramps), 70-120 m from the player (5-120
  m when starting), each a little to one side of the sidewalk's middle;
  left out beyond 140 m and drawn within 120 m.
- **Walking**: along the sidewalk at the walk's own speed (1.4 m/s for
  the men), and at its end on along another sidewalk starting or ending
  within 12 m (round the corner), else back.
- **Diving**: a car coming at one (over 3 m/s, passing within 2.4 m in
  the next 1.6 seconds) makes them dive away from its way (the walk to
  dive left or right, then onto the ground and up again, as the moves'
  table chains them), carried by the animations' own movement; up again,
  they walk on from the nearest point of their sidewalk. They aren't hit.

## In Reluva

`Pedestrians` (`src/engines/age/game/pedestrians.{h,cpp}`) places and
walks them, and `PedModel` (`src/engines/age/render/ped_model.{h,cpp}`)
animates and draws them.

## Sources and checks

- **Where the facts come from:** the aimaps, the road maps' sidewalks, the pedestrians' files ([ped.md](../formats/ped.md)).
- **How it was checked:** `age.unit.city`; by hand.
