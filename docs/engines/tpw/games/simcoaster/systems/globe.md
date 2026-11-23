# SimCoaster: choosing a world on the globe

> **Status:** partial · **Code:** `src/engines/tpw/ui/globe.cpp` · **Checked against:** a recording · **Confidence:** seen, not traced unless marked

## Overview

At the end of the front end's camera path ([front-end.md](front-end.md))
a globe has risen out of the boardroom's table: the three worlds on one
ball, each a wedge with its rides and its gate. It turns behind the
Personnel File ([personnel-file.md](personnel-file.md)); after "Enter
Game" the player clicks a world, which turns to the camera, and the
camera flies down to its gate before the park loads. From 42 s to 1:25.7
in the recording
([research/footage.md](../research/footage.md#the-opening-second-by-second)).

## How it works

### The globe in the model

The globe is the part of the camera path's model
(`levels/wIntro/obstacles/Camera/camera.MD2`) under its helper
`baseobject`: a piece for each ride, named for its world in Theme Park
World's terms, `ns` (New Science) for Land of Invention, `iw` (Ice
World) for Polar Zone, `an` (Arabian) for Arabian Nights, with the ride
on it and its patch of ground: `ns01(victorianhut)`, `iw02(whalespout)`,
`an03(gate)`. Each world has a gate, `ns02(gate)`, `iw04(gate)` and
`an03(gate)`, lying 120° apart round the globe's upright axis: Land of
Invention's toward the hall, −z, Polar Zone's +x, Arabian Nights' −x.
The pieces of a world lie in the wedge about its gate.

`levels/<world>/features/atm/atm.MD2`, the parks' cash machine, has
the same globe in small.

### Turning

Measured on the recording:

- From the path's end (42 s), the camera comes down to the globe over
  about 4 seconds, to a close view from the hall's side, steeply above
  it, the globe filling most of the screen.
- The globe turns about its upright axis, Land of Invention facing the
  camera as the path ends, the face moving to the left: 120° in 4
  seconds, Polar Zone facing at about 52 s.
- After "Enter Game" (1:22) the player points at Land of Invention; the
  globe turns faster, about 75° a second, until it faces the camera
  (1:23.2), holds it 0.8 seconds, then the camera flies down to its gate
  in about 1.4 seconds, "- PLEASE WAIT -" over the screen for its last
  0.3 (1:25.4), then the loading screen ([loading-screen.md](loading-screen.md)).

Which world is clicked isn't seen: Land of Invention, the only one open
to a new player, is the one the globe brings round. "- PLEASE WAIT -"
is in none of the game's files: its program draws it, in thin capitals
like `MENUBIG`'s, spaced out, 336 pixels across at 640 × 480.

## In Reluva

`Globe` (`src/engines/tpw/ui/globe.cpp`) finds the globe's pieces and
gates in the model, turns them about the axis through their middle, and
gives the cameras: the close view (from −z, 0.95 radians above level, 1.6
of the globe's radii away, aimed 0.3 radii below its middle, a lens of
0.7 screen heights), a blend from the path's last camera to it, and the
flight to a gate. A piece belongs to the world whose gate's direction
from the axis is nearest its own. `GlobeTurning` turns it by the clock
behind the Personnel File; `chooseWorld` takes a click: the world drawn
under the pointer, if open, turns to the camera the shorter way round,
then the flight and "- PLEASE WAIT -" (`MENUBIG`, 9 pixels between the
letters). The world chosen isn't used yet: the loading screen follows,
and the park isn't built.

`reluva-tool tpw globe-shot <game folder> <out.png> <degrees> [approach]
[world gate]` draws the globe turned, from the close view, on the way to
it, or on the way to a gate.

## Sources and checks

- **Where the facts come from:** the camera path model's node and mesh
  names and its pieces' places; the recording, 42 s to 1:25.7, every
  half second.
- **How it was checked:** `globe-shot` set beside the recording: at 0°,
  Land of Invention facing the camera as at 1:23.6, sand to its left
  and ice behind to its right; at 120°, Polar Zone facing as at 52 s.
  `front-end-run` clicks the middle of the screen a second after "Enter
  Game" and gets Land of Invention.

## Open questions

- The lit hole the globe stands in: the table's middle opens, it seems,
  by the animation's vertex tracks, which Reluva doesn't play yet
  ([md2-animation.md](../formats/md2-animation.md#open-questions)).
- What the game shows over a world under the pointer; what a click on a
  closed world does; which way, and how fast, it turns a world round.
- The close view and the timings, measured rather than read from the
  files.
