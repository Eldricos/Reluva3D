# SimCoaster: research and upgrades

> **Status:** partial · **Code:** `src/engines/tpw/park/research.cpp`, `src/engines/tpw/ui/park_windows.cpp` · **Checked against:** the recording, the general settings · **Confidence:** guessed unless marked

## Overview

A park's scientists research what isn't on sale yet: attractions, and
the upgrades of those that are. The work goes on in a Research Lab
(`UsageInfo.ThinkingSpace`), which the Staff menu's Research Lab (834)
opens once there is one. A project costs its research points, paid for in
dollars as it goes, and when it's done its attraction is on sale, or its
upgrade can be bought from the attraction's menu. In the recording the
player builds a lab, hires scientists and starts a project from 14:40 to
14:52.

## How it works

### Projects

The projects to choose from (*inferred* from the recording's list at
14:42):

- each attraction of the world not on sale whose `Research.Group` is past
  0 and which has a price; its cost, its `Upgrades[0].CostOfResearch`;
- each upgrade of an attraction on sale, `Upgrades[1]` and `[2]` with a
  `CostOfUpgrade`, whose `CostOfResearch` is past 0; its cost, that.

Both are scaled by `Research.CostScaleUp` (1). The list's columns are
the project, its type (`UITEXT` 313 to 320: Ride, Sideshow, Shop,
Feature, Facility, Park expansion, Ride upgrade), its consultant, its
estimated time as a bar, and its cost. The recording's rows match the
settings: the Bellows Bouncer's upgrades at § 100, the Bungee Boing at
§ 300.

The lab has two projects at a time *(inferred: its window's two
slots)*.

### Consultants

An attraction's `RStaff[0].SType` names the kind of staff who must
consult on its research: 1 a gardener, 3 an engineer and 5 a guard, by
the recording's list of projects and their consultants. The others are
guessed in the order of the staff types' strings: 2 a janitor, 4 an
entertainer, 6 a scientist. The project can't be started without one
of the kind employed (`UITEXT` 330, 331); Reluva takes the first of the
kind *(guessed)*. A project is held up (300 to 304) while its scientists
are on strike, none are assigned or in the park, or its consultant's
kind is on strike or none is employed.

### The work

Each day, each scientist assigned and in the lab adds their research
ability, `Researcher.MinResearchAbility` to `MaxResearchAbility` (2 to
6) by experience, times the work rate (`Research.StartingWorkLoad`, 85
percent, which the lab's slider changes) and `Research.SpeedScaleDown`
(0.005). Reluva scales that so one scientist of ability 2 does the
recording's § 100 project in about 30 days, as its window says ("Days
to go: about 30") *(guessed)*. The points are paid for at a dollar each, as "Expenditure remaining:
${14}" (297) suggests *(guessed)*. Assigned, a scientist
walks to the lab and stays there till the project is done or they're
taken off it.

### Upgrades

An attraction's levels are its `Upgrades[0]` to `[2]`, each with its own
`InitCapacity`, `InitDuration`, `WearRate` and `RunningCost`. The menu's
"Buy upgrade" (529) buys the next level's `CostOfUpgrade`, once:

- its research is done, if it has any;
- the park has an engineer trained in Ride Upgrading (`UITEXT` 11);
  Reluva's engineers learn it from any training *(guessed)*.

It's made over its `DurationOfUpgrade` (2 to 4) by 10 days *(guessed)*,
and may be cancelled (127) for the money back. Then the attraction is
at the level: its window says "Upgraded to level {3}" (38) where it said
"Not Upgraded" (37), and its script gets the level's capacity and
duration in `VAR_CAPACITY` and `VAR_DURATION` *(inferred)*. Fully
upgraded, the game says so (125).

### The windows

At 640 × 480, as the recording shows them:

| Window | Where |
|---|---|
| The Research Lab (14:52) | `!window_s` at (65, 20), 563 × 378, "Research Lab" (285). Two slots, (83, 66) and 145 lower, each with the project's name or "*** No Project ***" (291), a frame at (90, 103), 275 × 88, of its progress, days to go, expenditure and scientists assigned (295 to 298), its consultant (299) or none needed (293), what holds it up; Edit Project (286) at (374, 118) and Cancel Project (288) at (374, 152), 135 × 22. Start New Project (290) at (228, 355), 145 × 30. The work rate as a bar from (556, 66) to 250, + and − at its ends, "Work rate" (294) at (528, 320). The tick at (586, 357) |
| Choose a project (14:42) | `!window_s` at (65, 62), 563 wide, "Choose a project to research" (307). Its column buttons at y 118, 30 high: Project (95, 130 wide), Type (231, 78), Consultant (315, 105), Est. Time (426, 73), Cost (505, 67) (308 to 312). The list in a frame at (88, 153), 488 × 145, a row 13 pixels; a message frame at (85, 312), 452 × 42; the tick at (546, 322), cancel at (580, 322) |
| Assign staff | not in the recording: "Assign staff: {16}" (321), those available and those assigned (322, 323) with arrows between, the consultant (324, 325) and the other project's scientists (326, 327) *(guessed)* |

## In Reluva

`Park` (`src/engines/tpw/park/research.cpp`) keeps the projects
(`Park::projects`, `projectChoices`, `startProject`, `assign`), does the
day's research and upgrades (`Park::researchDay`), and buys, cancels,
deletes and renames attractions. `ResearchLabWindow`, `ProjectWindow`
and `AssignWindow` (`src/engines/tpw/ui/park_windows.cpp`) are the
windows; `playPark` opens the lab from the Staff menu, the chooser from
its Start New Project, and the assigning from the chooser's tick or Edit
Project. `reluva-tool tpw window-shot <game folder> <world folder>
<out.png> <lab|projects|assign>` draws them over a new park with a lab
and two scientists. The unit tests (`tpw.unit.park`) research a ride
and an upgrade, buy the upgrade with an engineer trained, and see it
made.

## Sources and checks

- **Where the facts come from:** the general settings (`Research.`,
  `Researcher.`); the attractions' settings (`Research.Group`,
  `Upgrades[n]`, `RStaff[0].SType`); the string tables; the recording at
  14:40 to 14:52.
- **How it was checked:** the lab's and chooser's pieces placed where
  the recording shows them, and `window-shot`'s pictures looked over;
  the chooser's projects and costs set against the recording's list.
  The pictures haven't been laid over the recording's frames.

## Open questions

- How fast research goes and what it costs; how the estimated time's bar
  is scaled; what the work rate does beyond speed.
- `RStaff`'s kinds past 1, 3 and 5; which staff member consults; what
  `{17}` in 324 is.
- What training gives Ride Upgrading; how long an upgrade takes.
- Park expansions (318) and "Something else" (320).
