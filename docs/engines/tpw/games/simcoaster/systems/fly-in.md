# SimCoaster: the fly-in

> **Status:** partial · **Code:** `src/engines/tpw/ui/fly_in.cpp`, `src/engines/tpw/ui/park_screen.cpp` · **Checked against:** the recording · **Confidence:** inferred unless marked

## Overview

After the loading screen, the camera flies low along the road into the
park: out of a tunnel, past the monorail's station and the ticket
booths, under the bunting, to the park's gates, golden, with blue
globes on posts, and rises a little over them; then the mission's
briefing shows over the park. In the recording from 1:34.5 to 1:50.5
([footage.md](../research/footage.md#the-road-into-the-park)).

## How it works

### The camera's path

Each world has an obstacle that's a camera path, "Camera Path"
(`obstacles/Camera/camera.sam`: `Info.Id` 1798, `Info.IsCameraPath`,
`Info.DontApplyOffset`), its model the same in all three worlds
(`camera.MD2`): a spline (`Camera pathr`, [md2.md](../formats/md2.md#a-spline))
and, on it, a dummy (`camera dummyr`), the camera. Its animation
(`cameram.MD2`, 500 frames) turns the dummy, never moves it: the dummy
goes along the spline, as a 3ds Max path constraint carries it. Laid
from the world's corner, the path starts in the tunnel on the road, 11.3
units up, about a cell, and ends over the gates' cells, risen to 42
units, the dummy turned 15° down to them. Its script waits to be set off
(`VAR_OBSTACLETRIG`) and plays the animation (`WAITANIM 5 0`).

- **The camera** looks along the dummy's own z, its y up; its lens as
  the front end's, 53° high.
- **Its pace:** each stretch between knots in the same time, as 3ds Max
  goes without constant velocity; at an even speed the camera falls
  behind the recording's, which passes the bunting at 1:42.
- **Its time:** 500 frames from leaving the tunnel at 1:34.5 to the
  camera's stop at about 1:47.5, 26 ms a frame, faster than the models'
  animations' 25 frames a second. The camera then holds on the gates for
  3 seconds, to the briefing at 1:50.5.

### The fixed items

The road's sights are objects of their own, laid from the world's corner
rather than a cell of the park (`Info.DontApplyOffset`: "a fixed item
whose animation should be played relative to world (0,0)"): the gates
(1601), the bunting (1705), the ticket booths' building (1707), the
helipad (1706), the fountain (1710), two installations (3708, 1709), the
cash machine and its desk (103, 104). The prebuilt park lists them,
unplaced ([tpws.md](../formats/tpws.md)). They're drawn as their scripts
pose them:

- most in the world's own space, as their models are laid out;
- those with a cell (`Info.EngineMapOffsetOverrideX` and `Y`: the gates'
  (46, 17)) from that cell's corner, stood up, their models standing
  along z. So the gates span the entrance, centred on the line between
  its two cells, and fill its cell's depth.
- The cash machine and its desk lie 8,000 units off, a scene of their
  own, not drawn in the park.

The gates' script opens them on `VAR_COMMAND` 1 (`WAITANIM 5 1`, then
`LOOPANIM 5 3`, `gatesm4.MD2`) and shuts them on 0; Reluva opens them
while the park's open, as the recording's are.

## In Reluva

`FlyIn` (`src/engines/tpw/ui/fly_in.cpp`) reads the path and gives the
camera; `playFlyIn` plays it over the new park before the briefing, its
objects' scripts running, the park's clock standing; a click or a key
ends it *(guessed)*. `ParkScreen::drawScene` draws the park without its
HUD. `reluva-tool tpw flyin-shot <game folder> <world folder> <out.png>
[frame]` draws a frame; the game-data test records frame 330.

## Sources and checks

- **Where the facts come from:** the camera paths' and fixed items'
  models, settings and scripts; the recording from 1:34 to 1:51.
- **How it was checked:** `flyin-shot` frames set beside the recording's
  every second: the tunnel, the road, the monorail, the booths, the
  bunting and the open gates in turn; the pace by when each comes.

## Open questions

- The camera's look between 1:38 and 1:40 drifts from the recording's,
  a little to the right; what else the game does with the path.
- The recording's sky is black through the fly-in; Reluva draws the
  world's sky.
- The road's forecourt before the gates is grey in the recording, green
  in Reluva, as the ground's painting is guessed ([lnd.md](../formats/lnd.md)).
- Whether the fly-in can be skipped, and how.
