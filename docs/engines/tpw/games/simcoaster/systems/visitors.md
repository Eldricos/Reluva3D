# SimCoaster: visitors in the park

> **Status:** partial · **Code:** `src/engines/tpw/park/visitors.cpp` · **Checked against:** the recording, Land of Invention's settings and scripts · **Confidence:** guessed unless marked

## Overview

Once in by the entrance ([park.md](park.md#visitors)), visitors walk
the park's paths, queue for its rides and ride them, paying each ride's
fare, then come off by its exit and walk on. In the recording the
park's visitors walk its paths and stand in its queues from 5:40 on,
and its money rises as they ride. The game's program can't be read, so
how they choose their way, and what they want, isn't known: Reluva's
visitors follow the simplest rules that give that.

## How it works

### Walking

A visitor walks from the middle of one path cell to the next. At each,
they go on to a neighbouring path cell, chosen by chance, but not back
the way they came unless it's the only way: at a dead end they turn
round.

### Queueing

A ride's queue ends at a path cell ([paths.md](paths.md#queues)).
Passing that cell, a visitor joins the queue one time in two, if there's
room (three to a cell, the entrance's included) and it isn't the ride
they last rode; after a ride, at each turn, one time in eight they
forget it. They walk up the queue, from the path, along its cells, to
the entrance, and stand behind the one in front, a third of a cell
apart; the first stands at the entrance.

### Riding

The first in the queue, at the entrance, is offered to the ride's
script, in `VAR_LETMEON` ([script-instructions.md](script-instructions.md));
when the script takes them (clears it), they're on the ride, unseen, and
pay its fare, its settings' `UsageInfo.InitPricePerUse` (the robot's
§ 10; rides' § 20 by their kind's settings). When the script lets them
off (`VAR_LETMEOFF`), they come off at the ride's exit, the `N` of its
footprint ([building.md](building.md#placing)), or its entrance for a
ride without one, and make straight for the nearest path cell, walking
on from there. That the game puts its guest at the front of the queue
in `VAR_LETMEON` and clears `VAR_LETMEOFF` as they go is the scripts'
own comments' *(inferred)*.

Which queue is whose: a ride's queue starts at its entrance, so it's the
queue of the attraction whose footprint has that entrance, placed or
the prebuilt park's ([tpws.md](../formats/tpws.md#the-objects)): the
prebuilt park's robot, the Dizzy Droid, and its queue *(confirmed)*.

## In Reluva

`Park` (`src/engines/tpw/park/visitors.cpp`) walks its visitors as
above, and keeps each queue's line; `ParkScreen::advance` offers the
first waiting at each ride's entrance to its script, run by `RideRun`
(`SimulatedHost::offerGuest`), and tells the park whom the script took
on (`Park::board`) and let off (`Park::letOff`). Riders aren't drawn.
Shops, sideshows and other attractions without a queue aren't visited,
and visitors have no money, wants or moods yet.

`reluva-tool tpw park-shot <game folder> levels/science <out.png> 72
0.6 0.6 50 44 25` shows the new park's first visitor in the robot's
queue, and lists each visitor, what they're doing and how many rides
they've had: by 4 minutes the first has ridden the robot. The unit
tests (`tpw.unit.park`) walk visitors along a path built in the test
into a ride's queue, on, and off.

## Sources and checks

- **Where the facts come from:** the scripts' comments on `VAR_LETMEON`
  and `VAR_LETMEOFF`; the rides' settings; the recording's queues.
- **How it was checked:** `park-shot` over the new park: its first
  visitor queues for the robot, is taken by its script, rides and comes
  off. Not against the game's own visitors.

## Open questions

- How the game's visitors choose where to go: the settings' wants
  (`PeepTypes`' preferred excitement, money, boredom), the attractions'
  `AttractionValue`, signs and the map.
- Whether a ride's exit needs a path, and where riders go from it.
- How many stand in a queue's cell, and how they're drawn waiting and
  riding.
- Shops, sideshows and facilities: how visitors use them.
