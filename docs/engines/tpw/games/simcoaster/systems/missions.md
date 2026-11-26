# SimCoaster: missions

> **Status:** partial · **Code:** `src/engines/tpw/park/missions.cpp`, `src/engines/tpw/ui/briefing.cpp` · **Checked against:** the missions' settings, the string tables, the recording · **Confidence:** guessed unless marked

## Overview

The game is a campaign of 18 missions, "Operation Expand!" to
"Operation Boss!" (`OPERATION.str`), one after another across the three
worlds. Each is set in a letter from the president (`MISSION_DESC.str`)
and asks for a statistic to be reached, an obstacle to be cleared, a
promotion or challenges won. Done, the president writes again
(`OBJECTIVE_COMPLETE.str`), and the next mission is the player's
objective: in the same world, or another, which it may open. The
Personnel File shows the objective and where it is ("Current Objective :
Operation Expand!, Land of Invention"). In the recording the player does
Operation Expand! and starts Operation Plughole!, researching the lake at
14:42.

## How it works

### The missions' settings

`missions/miss00.sam` to `miss17.sam`, the comments the makers'
own:

| Setting | What it is |
|---|---|
| `Completion.EndingTheme` | the world it's played in: 0 Arabian Nights, 1 Land of Invention, 2 Polar Zone ("0=AN 1=NS 2=WW") |
| `ExitStatistic[n].Type`, `TargetValue` | statistics to reach: 8 "get X visitors in park", 2 "balance (inc. loans) >= target", 6 park awards, 7 "Get 51% of shares"; 0 none |
| `Obstacle.Type` | the obstacle the mission clears, an object of the world's `obstacles` |
| `Obstacle.ResearchEffort` | "Effort taken to research object" |
| `ObstacleStaff[n].Ability`, `StaffType`, `Number` | staff it needs, so many of a kind trained in an ability (`ABILITIES.str`: 10 Water/Drainage) |
| `Obstacle.GoldenTickets` | golden tickets it needs |
| `Promotion.Number`, `RequiredChallenges` | a rank to reach (`PROMOTIONLEVELS.str`), challenges to win in a world |
| `Info.NoResearch` | no research in this mission |
| `TechLevel.NewMaxGroup`, `TechLevel.Theme` | "group to activate": the research groups allowed in that world |
| `Completion.ObstacleOpened` | an object that opens as it's done (the gates, 1700) |
| `Completion.ThemeOpened`, `ThemeNumber` | a world it opens (Polar Zone after Operation Manager!) |
| `Completion.LoadNewHoardings`, `BoundaryClicks` | the park's new land and fence |
| `Completion.EndOfGame` | the last: done, the game's won |

The objectives' statistics are counted as their names say: the visitors
in the park now; the bank balance less what's owed ("Profit = Bank
Balance less Outstanding Loans", `MISSION_DESC` 7; `STAT_TITLES` 7, "Make
a profit of ${0}"); the player's percentage of the shares; park awards.

`StaffType` numbers staff otherwise than the attractions' consultants
do ([research.md](research.md#consultants)): 0 a handyman ("Cleaner"),
1 a mechanic, 5 a gardener, by the missions' comments; the others
aren't used and are guessed.

### Obstacles

Obstacles are objects of their world (`obstacles/lake/lake.sam`:
`Info.IsObstacle`), placed in the prebuilt park: Land of Invention's
gates (1700), fallen trees (1701), river (1702) and lake (1703). An
obstacle's mission is done in steps, as `MISSION_DESC` 19 to 21 tell
Operation Plughole!'s:

1. Its solution is researched, a Park Expansion project at the mission's
   `ResearchEffort` with no consultant, as the recording's list shows
   ("'Operation Plughole'", Park expansion, None, § 100) *(confirmed)*.
   The lake's has its message: "I've made a Training Room available to
   you" (`TAG_SYSTEM` 255).
2. The staff it needs are trained in its abilities: from then a member of
   staff of a needed kind learns the first ability their kind still lacks
   in training, the Training Room's "Ability To Learn" *(guessed)*. The
   Training Room is on sale from then on.
3. Its golden tickets are there.
4. "Proceed with solution" (`UITEXT` 592), from the obstacle's right-click
   menu, beside "View objectives screen" (591), clears it: its
   `Obstacle.ReplaceWithID` put in its place (the lake for the drained
   lake, 1704), or it's gone; the obstacle's `Upgrades[0].CostOfUpgrade`
   (§ 150) paid and its golden tickets given up *(guessed)*.

The game has the player pick the staff (`UITEXT` 695, "Select An
Employee") and shows them at work; Reluva takes the first trained, and
clears it at once.

An obstacle that's `Obstacle.OpenedAutomaticly`, like the gates, opens
when its mission's objectives are reached.

### Doing a mission

A mission without an obstacle to solve is done, checked daily, once each
statistic is reached and the player has its rank, challenges and golden
tickets. The president's letter for it (`OBJECTIVE_COMPLETE` by the
mission's number, under "Objective Complete!", `TAG_SYSTEM` 168) shows;
the object it opens goes; the world it opens is opened; and the next
mission is the objective. If it's played in the same world, it begins
there and then, and its briefing shows; if not, the briefing says
"You have successfully completed your current objective! To progress to
the next area go to {5}." (`UITEXT` 76). The last mission done, the game
is won ("Congratulations - You've Won!", `TAG_SYSTEM` 163, 164).

### A park as the missions leave it

Reluva doesn't keep parks yet, so each is set up anew from the world's
prebuilt park as the player's progress leaves it: the obstacles and
gates of the missions done in that world are gone (replaced, as the
lake), the missions' research groups allowed, the Training Room on sale
if one of them needed trained staff. The objective is played if it's in
this world; elsewhere, the park has none ("Your current objective is in
{5}.", 75).

### The briefing

The briefing's details ([briefing.md](briefing.md)) list the
requirements and their status: each statistic (`STAT_TITLES` 5 to 12);
"Research the Park Expansion:" (`UITEXT` 90), with "Scientists need to
research a solution" (77) or how far the research is (91), then "TRAINED
STAFF REQUIRED:" (84), a line for each need (its number, kind, ability,
and those trained of it), and whether they're all there (92, 93);
"Gain promotion to {7}" (79) and "Current position: {12}" (98); "Complete
{9} of the {5} challenges" (80); "GOLDEN TICKETS NEEDED:" (85) with those
needed and held (`STAT_TITLES` 13 to 15). Plughole's letter changes as
the research goes and is done, and its staff are trained (`MISSION_DESC`
19 to 21); the others', once researched, add "Update: We've found a
solution!" (`UITEXT` 81).

## In Reluva

`Park` reads every mission and keeps the campaign
(`src/engines/tpw/park/missions.cpp`): `Park::load` takes the player's
objective, the first mission not done; `mission()` is the one played
here, `progress()` the objective, `proceedWithSolution()` clears the
obstacle. As the park closes, the objective, golden tickets and worlds
opened go to the player's file ([players.md](players.md)). The obstacles are read with the attractions
([building.md](building.md#what-a-world-has)), and drawn by their models.
`reluva-tool tpw park-shot`, `briefing-shot` and `window-shot` take
`-mission <n>` to set a park up as the player's campaign stands at a
mission. The unit tests (`tpw.unit.park`) play Operation Expand!,
Plughole and Manager on a small world built in the test.

## Sources and checks

- **Where the facts come from:** the missions' settings and comments;
  the obstacles' settings; `OPERATION.str`, `MISSION_DESC.str`,
  `OBJECTIVE_COMPLETE.str`, `STAT_TITLES.str`, `ABILITIES.str`,
  `UITEXT.str` and `TAG_SYSTEM.str`; the recording's project list at
  14:42.
- **How it was checked:** the research list beside the recording's at
  14:42; `park-shot -mission 2` against `-mission 0` over Land of
  Invention: the gates gone, the lake drained; the briefings' details for
  missions 1, 3 and 7 read over.

## Open questions

- Park awards aren't given yet, so the last mission can't be done.
  Challenges give the golden tickets and promotions, the special rides'
  the promotions past Manager ([challenges.md](challenges.md),
  [special-rides.md](special-rides.md)).
- How the park's land and fence change (`BoundaryClicks`, `hoardings.map`,
  `Hoardings.sam`, `EndHoardings.sam`), and what an obstacle's shape and
  `AfterShape` mark.
- How the player picks the staff, how long they work, and what the
  clearing shows (`Info.DestroyParticleEffect`, the obstacles' scripts);
  what clearing costs.
- Which ability training teaches, and whether research reveals it (the
  scientists' "new Excavation ability", `TAG_SYSTEM` 157 to 160).
- What `TechLevel.Theme` does when it's not the mission's own world.
