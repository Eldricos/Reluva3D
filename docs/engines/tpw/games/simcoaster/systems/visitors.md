# SimCoaster: visitors in the park

> **Status:** partial · **Code:** `src/engines/tpw/park/visitors.cpp` · **Checked against:** the recording, Land of Invention's settings, scripts and models · **Confidence:** guessed unless marked

## Overview

Once in by the entrance ([park.md](park.md#visitors)), visitors walk
the park's paths to its rides, sideshows, shops and toilets, queue,
pay, use them, and come out happier, fed or relieved; when they've had
enough, they go home. In the recording the park's visitors walk its
paths and stand in its queues from 5:40 on, its riders are seen on the
rides, and its money rises as they pay. The game's program can't be
read, so how they decide isn't known. The general settings
(`levels/Standard.sam`) give the numbers, many with their makers'
comments: Reluva's visitors use those numbers by the simplest rules
that fit them.

## How it works

### Who comes

Each visitor is a child, a parent or a grandparent by the settings'
shares (`PeepInfo.PercentageOfKids` 70, `OfParents` 20,
`OfGrandparents` 10), and one of that generation's 8 types
(`PeepTypes`), by chance. A type has the excitement it likes (1 to 99),
its money (100 to 900, give or take `StartingCashVarPc`, 15 percent),
its boredom threshold (30 to 95) and its pace. They pay the admission
from their money.

### What they need

Each has a happiness, starting at 50, and a hunger, a thirst and a need
of the toilet, from 0 to 100, each growing a quarter a day (a day is 4
seconds: from nothing to their most over the 400 days of
`PeepInfo.ExitLevel`). A need at 80 or more (`ThoughtBubbleHungry` and
the like) takes a point of happiness a day.

### What they choose

At each path cell's middle, a visitor without a place to go scores
every attraction they can choose (`Info.IsChoosable`), reach along the
path, pay for, and that has room, and makes for the best, if any scores
above nothing:

- a ride or sideshow: 20, and `DecisionVarExcitementWeight` (1) for
  every point its excitement (`UsageInfo.ExcitementLevel`) is nearer
  than 40 (`OKExcitementMatch`) to what they like;
- a shop or toilet: for each need it meets (a drink's
  `ThirstEffect`, food's `HungerEffect`, a toilet's `ProvidesRelief`),
  that need's weight (`DecisionVarThirstWeight` and the like, 2) times
  half the need past 30; a shop meeting none (balloons, gifts) its
  `HappinessEffect`;
- less `DecisionVarDistWeight` (1) for each two cells of path to walk,
  and `DecisionVarQueueWeight` (1) for each one in its queue.

What they used last isn't chosen again until, one turn in eight, they
forget it. They follow the shortest path to it; without a place to go
they wander, by chance, not back the way they came unless it's the only
way.

### Queueing

A ride's queue ends at a path cell ([paths.md](paths.md#queues)); an
attraction without one (a shop, a sideshow, a toilet) is reached from a
path cell beside its entrance. They walk up the queue to the entrance
and stand behind the one in front, a third of a cell apart, three to a
cell (the entrance's included).

### Riding

The first in line, at the entrance, is offered to the attraction's
script in `VAR_LETMEON` ([script-instructions.md](script-instructions.md));
when the script takes them (clears it), they're on, and pay its fare,
its settings' `UsageInfo.InitPricePerUse` (the robot's § 10, a drink's
§ 30). When the script lets them off (`VAR_LETMEOFF`) they come off at
its exit, the `N` of its footprint ([building.md](building.md#placing)),
or its entrance for one without, and make for the nearest path cell. The
shops' scripts take a guest so too (`COPY VAR_PEEPID VAR_LETMEON`, then
`WAIT 3000` or `LIMBO VAR_LETMEON 5`, then into `VAR_LETMEOFF`); that
the game puts its guest at the front of the queue in `VAR_LETMEON` and
clears `VAR_LETMEOFF` as they go is the scripts' own comments'
*(inferred)*. An attraction without a script keeps each for 3 seconds,
as the shops' scripts do.

Coming off, what it did to them is done:

- a ride or sideshow: `PeepInfo.PerfectRide` (25) happiness if its
  excitement is within `PerfectExcitementMatch` (5) of what they like,
  `GoodRide` (15) within `GoodExcitementMatch` (15), `OKRide` (5) within
  `OKExcitementMatch` (40), as the settings' comments say *(inferred)*;
- its `HappinessEffect` added, its `HungerEffect` and `ThirstEffect`
  taken from their hunger and thirst, its `ToiletEffect` added to their
  need of the toilet, which a toilet (`ProvidesRelief`) empties.

### On the ride

The models mark their seats with helpers ([md2.md](../formats/md2.md#the-nodes)):
`Head01` up, `SpriteHead01` on the robot. A ride's script shows its
rider (`ADDHEAD` as it seats them, `DELHEAD` as they leave); each is
drawn in the first seat free, by their sprite, standing, their head at
the seat's helper, moving with the ride. A bouncy ride's bouncers
(`BOUNCE`) are drawn at its `Sprite 01` helpers in the order they got on,
their middle there, bouncing as its animation moves them. Those
`UsageInfo.RideHandlesSprite` keeps in sight, as most sideshows do,
stand at the entrance while they use it; the rest are out of sight
inside.

### Going home

A visitor goes home, along the path to the end of the way in and out
along it, when their stay is up (`ExitLevel` 400 days, give or take
`ExitLevelVar` 60), when their happiness is gone, or when they've done
nothing for more days than their boredom threshold: with no money left
for anything, they soon are. One in a queue whose stay or happiness is
up leaves it first. Others come to take their places
([park.md](park.md#visitors)).

## In Reluva

`Park` (`src/engines/tpw/park/visitors.cpp`) keeps its visitors and
their needs, chooses for them, walks them, and keeps each queue's line;
an attraction without a queue has a kiosk, a queue of no cells.
`ParkScreen::advance` offers the first waiting at each entrance to the
attraction's script, run by `RideRun` (`SimulatedHost::offerGuest`),
and tells the park whom it took on (`Park::board`) and let off
(`Park::letOff`); `ParkScreen` draws riders at the seats the script
shows them in (`SimulatedHost::heads`) and bouncers where they bounce.
Sprites are drawn over the models, so a rider behind a part of one
still shows. Weather, litter, vomit, pranks, the visitors' thoughts and
their opinions of prices aren't done.

`reluva-tool tpw park-shot <game folder> levels/science <out.png> 300
[yaw pitch distance x y] [-place <id>]` runs a new park five minutes
on, an attraction placed by the path near the entrance (`-place 1203`
the drinks kiosk, `-place 1100` the Bellows Bouncer), and lists each
visitor: what they're doing, their rides, money, happiness and needs.
The unit tests (`tpw.unit.park`) walk visitors along a path built in the
test to the ride that matches what they like, on, off, happier, to a
kiosk, and home; `tpw.unit.script` offers a script a guest and has it
seat them and let them off.

## Sources and checks

- **Where the facts come from:** the general settings' `PeepInfo`,
  `PeepTypes` and `Arrival`, and their comments; the attractions'
  settings; the scripts' comments on `VAR_LETMEON` and `VAR_LETMEOFF`,
  and their `ADDHEAD` and `BOUNCE`; the models' helpers; the
  recording's queues and riders.
- **How it was checked:** `park-shot` over the new park: its visitors
  queue for the robot and are seen in its seat, buy drinks when
  thirsty, bounce on the Bellows Bouncer, and go home. Not against the
  game's own visitors.

## Open questions

- How the game's visitors choose: what its `DecisionVariable`s and
  weights multiply, and how new rides count; what `BoredomThreshold`
  measures; how fast needs grow, and how happy they start.
- What `ExitLevel`'s "gameticks/4" are.
- Whether a ride's exit needs a path, and where riders go from it.
- How many stand in a queue's cell; how they're drawn in it and in the
  seats (sitting, turned with the ride).
- Gender: the toilets' `GenderSpecific`.
