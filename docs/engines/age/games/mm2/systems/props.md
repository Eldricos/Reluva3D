# Midtown Madness 2: props, and knocking things down

> **Status:** done · **Code:** `src/engines/age/game/props.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

The props and street furniture, and knocking them and the traffic lights down. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

The props of `city/<city>/props.pathset` ([pathset.md](../formats/pathset.md)),
and in a race its own (`race/<city>/circuit0.pathset`...: the circuits'
barricades, a lesson's parked cars and ramps), and the street furniture
the cities' rules line the sidewalks with (street lights, meters, bins,
phone boxes...), stand where they put them, and they and the traffic lights can be knocked
down (`props.cpp`):

- Each is collided with: its bound (`bound/<model>_BOUND.bnd`), else its
  model's box; a traffic light its pole (0.4 m square, the model's
  height).
- `tune/banger/<model>.dgBangerData` gives its **Mass** and
  **ImpulseLimit2**. ImpulseLimit2 is the mass times one of a few
  numbers (31.25, 500, 800, 2,500, 10,000; 10^30 for trees and the
  like, which never come down); Reluva reads it as the square of the
  impulse that breaks the prop loose: a lamp of 836 kg, 818 kg m/s; a
  cone (10.6 kg), 92; a parked car (10,000 x 800 kg), 2,830.
- A car's knock over that (CarSim's hull against the prop's triangles)
  breaks it loose: the car isn't stopped as by a wall, but loses what an
  inelastic knock with a body of the prop's mass would take. The prop is
  then out of the way (out of the collision world), falls over about its
  foot the way it was pushed if it's taller than 1.2 m, and slides to a
  stop (6 m/s/s). Cars, opponents and police knock things down alike.
- A prop with pieces comes apart instead: its banger's **NumParts**
  names `<model>_BREAK01.dgBangerData`... and its model's `BREAK01`...
  meshes (a lamp's post and arm, a barricade's posts and boards, a
  sign's post and board). Each piece's mesh is modelled about its own
  middle, and its banger's **CG** puts that middle where it is from the
  prop's foot (the lamp's arm, CG x -1.14 and 2.79 wide, spans the whole
  model's -2.5..0.3), so that's where each starts. They fly off the way
  the car went (at half to 80% of its speed, up 1.5-2.5 m/s, turning),
  fall, bounce a little on the ground under them, and come to rest.
  Standing, a model's pieces aren't drawn with it (they'd be at its
  foot).
- What flies out: the banger's **BirthRule** block (the
  `tune/effects` rules' fields) and **TexNumber**, the texture
  `texture/fxpt<TexNumber>`: phone booths and parking meters throw
  coins (`fxpt2`), bins litter (`fxpt3`), mailboxes and news boxes
  papers (`fxpt7`), trees leaves (`fxpt5`). About 41 bangers have an
  **InitialBlast** or a **SpewRate**; breaking, the prop throws its
  blast at once (at least one) from its middle plus the rule's
  **Position**, each a frame between TexFrameStart and TexFrameEnd of
  the texture's (the 128-pixel ones are four frames, two by two), then
  spews for **SpewTimeLimit** seconds. Its pieces throw their own as
  they fly off (a car door's glass).
- **NumGlows** and **GlowOffset**: 19 bangers have one lamp (the street,
  park, freeway, Thames and Chinatown lamps, the banner lamps, a lit
  sawhorse; two are pieces, a sign's, whose lamps light while it stands), the offset from
  the model's middle (the street lamp's 3.65 up, the freeway lamp's
  17.8). At night Reluva draws a warm flare there while the prop
  stands.
- **Elasticity** is 0.5 for 989 of the 992 bangers, 1.5 for the traffic
  light and the parking meter, 0.9 for a banner lamp: a standing prop's
  surface bounces a car by its share of 0.5 (three times as much off a
  traffic light). **Friction** is 0.9 for all, the city's default, so it
  changes nothing.
- **Flash** isn't used: 0 for 652 bangers, else numbers that run in
  batches, the same within a model's set (306-314 on the traffic's
  HEADLIGHT bangers, 188 on all four of the police car's sirens, 684-688
  on some London props): a stamp of some kind, not a setting.

## In Reluva

`Props` and `BangerData` (`src/engines/age/game/props.{h,cpp}`). Reading
ImpulseLimit2 as the square of the breaking impulse is Reluva's
(above).

## Sources and checks

- **Where the facts come from:** `props.pathset`, `proprules.csv`, `tune/banger/*.dgBangerData`, the models' BREAK pieces.
- **How it was checked:** `age.unit.city`; by hand.
