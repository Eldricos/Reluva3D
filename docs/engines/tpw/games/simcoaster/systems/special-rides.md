# SimCoaster: special rides

> **Status:** partial · **Code:** `src/engines/tpw/park/special_rides.cpp`, `src/engines/tpw/ui/info_windows.cpp` · **Checked against:** the settings, the string tables · **Confidence:** guessed unless marked

## Overview

Each world has a few special rides standing in its prebuilt park,
boarded up: Land of Invention's Giant Waterwheel (1160), Meka Bird
(1170), Steaming Log Mill (1180) and a boating lake also named "Meka Bird"
(1110: "Is this the lake/boat ride?", its settings' comment asks); Polar
Zone's and Arabian Nights' likewise. A special ride is activated with
golden tickets and staff trained in the abilities it needs, and then it
runs as any ride does. Its challenge, set once it's open, gives the
promotions to Senior Manager and above ([challenges.md](challenges.md)),
which missions 4, 10 and 14 need ([missions.md](missions.md)).

## How it works

### The settings

A special ride's own settings (`rides/vict_hs/vict_hs.sam`):

| Setting | What it is |
|---|---|
| `Info.IsSpecial` | 1 |
| `UsageInfo.GoldenTicketCost` | "Golden Tickets required to open" (1 each) |
| `SkillSet[n].Num` | "skill and number of staff required": so many staff with ability n (`ABILITIES.str`); where it's given twice the last counts |
| `Info.Hoarding` | its hoarding's shape, which Reluva doesn't draw yet |

Land of Invention's need First Aid (9) and Landfill (11) for the
boating lake, First Aid for the waterwheel, Excavation (13) and Ride
Upgrading (1) for the Meka Bird, Excavation and Demolition (4) for the
log mill. The tour rides of Polar Zone and Arabian Nights cost a ticket
and no staff, and aren't marked special; Reluva takes a golden-ticket
cost as making a ride special.

### Abilities

Each ability is learnt by one kind of staff in training: as the missions'
`ObstacleStaff` pair them (mechanics construction, rock blasting,
demolition, tunnelling and stonemasonry; handymen rubble removal;
gardeners water/drainage, landfill, landscaping, excavation and
logging), engineers ride upgrading, janitors firefighting ("train a
janitor so he can extinguish", `TAG_SYSTEM` 116) and guards first aid
("Call guard for first aid", `UITEXT` 659) *(inferred)*.

An ability must be discovered before staff can learn it ("Undiscovered
ability!", 114). The scientists discover them in their research for the
missions' obstacles ("Your scientists have discovered the Landscaping
ability", `TAG_SYSTEM` 157 to 160): Reluva takes an ability as discovered
once an obstacle needing it has been researched, and ride upgrading,
firefighting and first aid as known from the start *(guessed: no
obstacle needs them)*.

The game has the player choose what a member of staff learns ("Select
Ability To Learn", `UITEXT` 705 to 712); Reluva picks what the current
mission's obstacle needs of their kind, else what a special ride waiting
to be activated does, else ride upgrading for an engineer.

### Activating

Clicking a special ride that's boarded up opens its window ("Special
Ride: {0}", 116): "You will need staff trained in the following abilities
to activate this ride. Suitable employees' names will appear in the
'Assigned' column" (985), a row for each member of staff needed with
their kind, the ability or "Undiscovered ability!", and the one assigned
(982, 697, 983); "You also need {50} Golden Ticket (you currently have
{51})" (986, 987); and the note that some abilities aren't discovered yet
(115). The tick activates it, if all's there: its golden tickets are
given up and it opens. Reluva assigns the first trained of each kind, as
it does an obstacle's staff, and lays the window out by guess: the
recording opens none. A special ride can't be opened from its menu
before it's activated.

The player's file keeps the special rides activated
([players.md](players.md)), so each new park of theirs has them open.

### Their challenges

Each world's last four challenges are its special rides' (`TargetObj`),
offered once the ride is activated. As their letters ask
(`CHALLENGE_SPECIFIC.str`):

| Type | Challenge | Asks |
|---|---|---|
| 17 | 8, 22, 30 | so many visitors use the ride |
| 48 | 20 (the boating lake; Senior Manager) | its customers' satisfaction above so much, no pranks |
| 50 | 24 | happiness among the three commonest thoughts |
| 52 | 23 | so many users, its repair above `TargetVal2` |
| 53 | 27 | its excitement, its repair above `TargetVal2` |
| 55 | 28 (Genie Spinner; Assistant Managing Director) | its excitement at its most, "Too messy" not among the commonest thoughts |
| 56 | 21 (the log mill; Managing Director) | its excitement, its satisfaction above `TargetVal2` |
| 58 | 25 | so many percent of the park's visitors use it |
| 51, 57 | 29, 26 | the coaster's loops, its users' photos: no coasters or photos yet, so they're never offered |

A ride's excitement is its `ExcitementLevel`, as there are no sliders to
change it yet; satisfaction is the visitors' happiness *(guessed)*.

## In Reluva

`Park::activate`, `activationStaff`, `abilityDiscovered` and
`abilityToLearn` (`src/engines/tpw/park/special_rides.cpp`,
`missions.cpp`); `ActivateRideWindow`
(`src/engines/tpw/ui/info_windows.cpp`). `reluva-tool tpw window-shot
<game folder> <world folder> <out.png> activate [-mission <n>]` draws the
window for a world's last special ride. The unit tests (`tpw.unit.park`)
discover an ability with a mission's research, train a guard and a
gardener in a boating lake's abilities, activate it and win its
challenge's promotion.

## Sources and checks

- **Where the facts come from:** the rides' and missions' settings;
  `ABILITIES.str`, `UITEXT.str`, `TAG_SYSTEM.str`,
  `CHALLENGE_SPECIFIC.str`.
- **How it was checked:** `window-shot activate -mission 2` read over:
  the boating lake's First Aid and two Landfill, the latter undiscovered
  till Polar Zone's swamp is researched. None of it has been set against
  the game.

## Open questions

- How an ability is discovered, and which; how the player chooses one to
  learn.
- The hoardings, and what activating shows.
- What the special rides' challenges measure: satisfaction, excitement.
- Whether a ride costs anything else to activate.
