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
(`PeepTypes`), by chance. Who they are is one of the settings' people,
numbered as the umbrellas' heights over them are
(`PersonSpriteUmbrellaModifiers[n]`), whose comments say who: 0 a boy,
1 a girl with brunette pigtails, 2 a blonde girl with a cap, 3 a boy
with a yellow cap; 4 the fat bloke, 5 the tall woman; 8 the old biddy,
9 the old bloke. Each is drawn by their sprite
([sprites.md](../formats/sprites.md)): `SPR_BD`, `SPR_GB`, `SPR_GK`,
`SPR_KB`, `SPR_FA`, `SPR_MO`, `SPR_GM`, `SPR_GR`, matched to the comments
by their look *(inferred)*. Toilets are for one or the other
(`Info.GenderSpecific`: 1 "for boys", 2 "for girls", their comments
say), and visitors use only theirs. A type has the excitement it likes (1 to 99),
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
  and `DecisionVarQueueWeight` (1) for each one in its queue;
- scaled by its `WeatherModifier` percentage in the weather
  ([weather.md](weather.md)), and by its `GenerationModifier` percentage
  for their generation (`KidScorePercentage`, `ParentScorePercentage`,
  `GrandparentScorePercentage`: 0 for the grandparents on some rides);
- scaled by what they think of its price (below).

A shop selling what they carry already (`UsageInfo.AppearanceEffect`:
1 a balloon, 2 a costume, 3 an umbrella) isn't chosen, nor a costume by
a grown-up: the costume shop's comment says "give the kid a costume",
and the worlds' costumed sprites (`Science/Costumes/SPR_GK`, and its
head) are a child's, which a costumed child is drawn as.

What they used last isn't chosen again until, one turn in eight, they
forget it. They follow the shortest path to it; without a place to go
they wander, by chance, not back the way they came unless it's the only
way.

### What they think of prices

What they think fair for an attraction is its `Info.ExpectedPrice`; for
coming in, the park's attractions' excitement over
`PeepInfo.ExcitementToCostDivisor` (6, "scaling factor for entry fee"),
not below `MinimumEntryFee` (20, "below which everybody always pays up
happily") *(guessed)*. A price up to `CheapPriceMultiplier` (0.75) of
fair is cheap, to `AveragePriceMultiplier` (1.25) about right, below
`ExpensivePriceMultiplier` (2) too high, and from it much too high, as
the settings' comments say; their opinions of each are
`OpinionForTooLow` 100, `AboutRight` 50, `TooHigh` 20, `MuchTooHigh` 0.
Reluva makes an opinion a change of happiness as they pay, a tenth of
its difference from about right's (cheap +5, too high −3); scales what
they want of an attraction by it; and has them pass by an attraction,
or turn back at the gate, that is much too dear *(guessed)*.

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
  need of the toilet, which a toilet (`ProvidesRelief`) empties;
- a ride's excitement over `RideVomitDivisor` (10) added to their
  sickness, as that setting's comment says, and its `VomitEffect`;
- its `LitterEffect` carried away as litter; what it sells
  (`AppearanceEffect`) carried, an umbrella over them, a balloon on a
  string;
- a ride's "I liked that!", or "I didn't like that" if its excitement
  was more than `OKExcitementMatch` from what they like.

### On the ride

The models mark their seats with helpers ([md2.md](../formats/md2.md#the-nodes)):
`Head01` up, `SpriteHead01` on the robot. A ride's script shows its
rider (`ADDHEAD` as it seats them, `DELHEAD` as they leave); each is
drawn in the first seat free, by their head alone, the heads' sprites
(`Kidsheads/SPR_KB` and the like, [sprites.md](../formats/sprites.md#heads)),
at the seat's helper, moving with the ride: the head's tilt by how the
camera looks down on it, its heading by the way the seat faces, the
model's front *(guessed)*. A bouncy ride's bouncers
(`BOUNCE`) are drawn at its `Sprite 01` helpers in the order they got on,
their middle there, bouncing as its animation moves them. Those
`UsageInfo.RideHandlesSprite` keeps in sight, as most sideshows do,
stand at the entrance while they use it; the rest are out of sight
inside.

### Litter and sickness

Litter carried is dropped into a bin (`UsageInfo.HoldsLitter`: the litter
bin) within 2 cells as they pass, or else onto the path, one cell in so
many by its `LitterEffect` (a chip shop's 40: one cell in ten). At a
cell with their sickness past `VomitCapacity` (100), they're sick on
the path: they lose `MediumHappinessChange` (15), those near them
`SmallHappinessChange` (5) and think it "Too messy". Sickness wears off a
tenth a day. Each day a visitor near litter loses a point of happiness.
Litter is drawn from `Litter/Spr_li`'s first 10 frames, sickness from
its last 2, by their look *(guessed)*. Janitors sweep it up
([staff.md](staff.md)).

### Pranks and whining

One visitor in `PrankeryLikelihood` (10) of a hundred is a prankster.
Each day, a prankster under `PrankHappinessThreshold` (15) happiness,
with no security camera (`UsageInfo.ProvidesSecurity`) within 6 cells,
plays a prank `PrankLikelihood` (10) percent of the time: a stink bomb
`StinkbombLikelihood` (25) percent of those, its cloud drawn by the
particles' white puff for 10 seconds, every visitor within 2 cells
`MediumHappinessChange` less happy and angry; otherwise litter thrown
down. Unhappy grown-ups whine, and those within `ViciousRumoursArea` (2)
cells of them lose a point a day ("whining", the thoughts say).

### Thoughts

Every 20 seconds or so, a visitor thinks how they are, shown for 4
seconds in a bubble over them: the first of sick (`ThoughtBubbleSick`
80), needing the bathroom, hungry and thirsty, hungry, thirsty (80 each),
disliking the weather, too messy, unhappy or whining (`ThoughtBubbleUnhappy`
20), bored, the line too long, liking the weather, very happy, happy
(`ThoughtBubbleHappy` 80), okay. What happens to them is thought at
once: a ride liked or not, being sick, a prank. The 21 thoughts are
`THOUGHTS.str`'s first, its bubbles the blue ones of `Thoughts/Spr_tb`
(16) and `Spr_tc` (its first 5; its pink ones are the staff's), matched
by what they show *(inferred)*; which comes first is guessed.

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
Sprites are hidden where the models drawn are nearer, by the
rasterizer's depth: the ground and a seat as near as a sprite don't hide
it. Staff clear the litter ([staff.md](staff.md)).

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
- How the game judges prices, and what `PcOfGoodPriceIsCheap` (70)
  does.
- How many stand in a queue's cell; which way riders' heads face.
- How the game chooses which thought to show, and how often; which
  litter frames are what; how fast sickness wears off; what
  `ThoughtBubbleMess` (0.3) measures; `WhingeDuration`.
