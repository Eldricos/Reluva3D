# Star Trek: Away Team: objectives and a mission's end

> **Status:** partial · **Code:** `src/engines/stawayteam/game/objectives.cpp`, `src/engines/stawayteam/ui/map_mission.cpp`, `src/engines/stawayteam/ui/objectives_window.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

Each mission has objectives: some it starts with, in its `.MDS`, and
some the map's triggers add. The triggers count them, complete them,
fail them, show and hide them. With every objective the mission needs
complete, its Win Action runs, which in Mission 01 beams the team out and
shows the mission completed. An objective failed, or an officer lost,
shows the mission failed. The objectives button opens a window listing
them; the End Game Menu ends the mission, going on to the next or
starting it again.

## How it works

### The mission's objectives

The `.MDS`'s `Objective` is a `CMissionObjectiveManager`: `Base Score
for Mission` (100), its `Win Action`, and `Objectives`, an array of
objectives (18 missions have one). Each objective (`CMissionObjective`)
has:

- **`Objective Name`**, by which the triggers name it.
- **`Mission Status`**: `Incomplete`, `Complete`, `Failed` or
  `Disabled`, counted 0 Disabled, 1 Incomplete, 2 Completed, 3 Failed
  where the list draws them (`0x47fa73`). All 74 in the missions start
  Incomplete.
- **`Display Status`**: `Visible` (54) or `Hidden` (20).
- **`Required To End Mission`**, `Points`, and its `Fail Action` and
  `Success Action`.
- **Its words:** a `CStandardMissionObjective`'s `Objective Script`
  (`Mission Objectives`, `Levels/Mission Objectives.RDS`) and `Objective
  Line`, the line's label ("Rescue the two Klingon"). A count's line has
  a `%d` for its count: "Rescue the two Klingon scientists that are being
  held captive. (%d of 2)".
- **Its kind:** a `CCountMissionObjective` (13 in the missions) has
  `Count Before Success` and `Current Count`; a `CProtectMissionObjective`
  `WhoToProtect`, a `CRescueMissionObjective` (4) `WhoToRescue`, a
  `CNeedItemMissionObjective` `Item Needed`. Rescues: Data (Mission 05),
  Worf (Mission 08, its Fail Action the mission failed: "You have allowed
  Ambassador..."), and Mission 13's added ones, `$instigator`, their line
  "Rescue %s.". The map's actions complete them, and so do their own
  rules, below.

### The objectives' own rules

The manager tells each objective still to do of every death (`0x5b7880`,
after the "Died" sequence, `0x494eae`) and of everyone joining the team
(`0x5be8e0`); each judges it (`0x50c870`):

- **Protect** (`0x50b610`): its whom dead, it's failed.
- **Rescue** (`0x50c0d0`): its whom dead, it's failed; its whom joining
  the team, it's complete (Mission 16's Brexen, taken off the team and
  then back; Data, Worf).
- **Need item** (`0x50bb20`): someone dead, and no one living on the
  team holding its `Item Needed`, used up or not, it's failed. None of
  the missions has one.

A protect's or rescue's `$trigger` or `$instigator` is made the name of
that part or officer as it's added (`0x50c270`). An objective's status
set (`0x50a1f0`): Failed runs its Fail Action, Complete its Success
Action, and the manager looks again: with every required objective not
Disabled complete, those still Incomplete are failed without their
actions, and its Win Action runs, once (`0x50c9d0`). A required one
failed doesn't end the mission by itself: the maps' Fail Actions do.

Mission 01 has three: rescue the two scientists (a count of 2, required),
free the pinned-down warriors (a count of 3, not required), and retrieve
the Romulan cloaking band (hidden, required).

### What the triggers do

Actions in the maps and missions (counted over all of them):

- **`CAddMissionObjectiveAction`** (113) adds its `Objective`. Mission
  01's spawn points' `CSpawnPointAI` each has a `Spawn Action` adding a
  `CProtectMissionObjective` named Protect, hidden, `WhoToProtect`
  `$instigator`, whose Fail Action shows the mission failed ("One of your
  Team") 4 seconds later.
- **`CSetMissionObjectiveStatusAction`** (158) sets an objective's
  `Status`: Complete, Failed, Incomplete or Disabled.
- **`CSetMissionObjectiveCountAction`** (158), its `Count` always 1,
  added to a count objective's `Current Count`; at its `Count Before
  Success` or past it, the count's held there and the objective's made
  Complete, whatever its status was (`0x50b100`). Its line's `%d`s are
  the count and the target (`0x50b0d9`).
- **`CSetMissionObjectiveDisplayStatusAction`** (29) shows or hides one.
- **`CSetNextMissionAction`** (17): the `Next Mission` ("Mission
  02/Mission 02").
- **`CDisplayMissionCompletedScreenAction`** (18) and
  **`CDisplayMissionFailedScreenAction`** (126): the mission's over, with
  the line `Mission Over Text` of the `Script` (`Mission Ending
  Messages`); the failed one's name is `Failed Message`, the completed
  one's `Completed Message` (`0x509385`).
- A part's **`Destroyed Action`** runs as it's killed: a Klingon
  scientist's fails the rescue and shows the mission failed.

### The objectives window

The objectives button, and the key bound to Display Objectives (O),
press the same button (`0x476c90`), which opens Rules.txt's `Objectives
Menu` (`CStarTrekObjectivesDisplay`, made at `0x51ad60`):

- **Its background** `Objectives Menu/Background`, and RESUME, of the
  escape menu's `Button Background`, 69 across and 27 above the
  background's bottom under its own height (`0x51aefb`).
- **The list** at `ObjectiveDisplay Pos` (64, 34), `ObjectiveDisplay
  Size` (287 wide), `Objective Display Spacing` (7) apart (`0x47f990`):
  "Primary Objectives" in `Trek Title Blue`, then each objective shown
  that's primary: its state's picture (`Objectives Menu/Disabled`,
  `Incomplete`, `Completed`, `Failed`), its words in `Trek Small`
  wrapped beside it 2 pixels on, the picture centred on them; the next
  under the taller. Then "Secondary Objectives" and the others. A
  disabled one is drawn at half brightness (`0x47fa7e`).

### The End Game Menu

The mission's end shows Rules.txt's `End Game Menu`
(`CStarTrekEndGameDisplay`, made at `0x50cfd0`): its background, the
mission's message in its `End Game Display Font` (`Trek Small`) wrapped
and centred across the top of `EndGameDisplay Pos` and `Size` (60, 26;
292 wide), the objectives' list 5 pixels under it (`0x50cef0`), and its
buttons, of `End Game Menu/Button`: completed, CONTINUE alone; failed,
LOAD GAME, RESTART and END GAME (`0x50d08d`).

It takes the map's input (`0x5ca130`: the count at the game's `+0x60c`
that the map's keys and mouse look at, `0x5c8820`) but, unlike the
objectives window, the escape menu, the save and load screens, isn't
counted on the program's holds: the game goes on under it, its
characters moving and its time counted (`0x5b597f`). Its buttons
(`0x50d8a0`):

- **CONTINUE** asks the game for its next step (`0x5c2a50`, step 4,
  `0x5c2b22`): the mission's `Debriefing Music` queued with a tenth of a
  second's fade, then its debriefing if it has one, and after it (step
  5, `0x5c2cd5`), with `End Game After Mission` (Mission 18 alone), the
  `EndGame` movie and the main menu; else the mission named by its last
  `CSetNextMissionAction` becomes the current one (`0x5b7560`) and
  begins, from its briefing. Every mission but the 18th names one.
- **END GAME** asks first: pressed once, it's marked (`0x50d98b`), and a
  second time it opens the main menu. Under the buttons, centred and 54
  above the window's foot (`0x50d6c5`, `0x50d7bd`), "Click 'End Game' to
  exit to the main menu." in `Trek Small`, and once pressed, "Click 'End
  Game' again to exit to the main menu." in `Trek Small Red`.
- **RESTART** (`0x50d8f6`) and **LOAD GAME** (`0x50d8bc`, the Load Game
  screen).

### The score

The game keeps a score (`CStarTrekSaveData`'s `Score`, at its `0xdc`),
saved with the game:

- **A mission's:** as it's completed (`0x5097fe`), its `Base Score for
  Mission` (100 in all 18) and each objective's `Points` that isn't
  required and is complete (`0x50ca70`): Mission 01's warriors' 30.
- **A new game begun at a later mission** (a developer's start) begins
  with 100 for each mission before (`0x5c3070`); restarting a mission
  keeps the score it had (`0x5c140a`, `0x5c1529`).
- **What it's for:** an item's `Points to Discover` (up to 2,400: the
  Phaser Damage Increase's 400, the Remote Mine's 250); 38 are 0 and 6
  -1. An item's found once the score's at least its Points to
  Discover, -1 never (`0x5b90c0`; a network game's own points
  otherwise), looked for as each mission starts (`0x5c319d`, and the
  briefing's `0x473dc6`, both calling `0x5b8fe0`): so the cloaking band,
  at 1, after the first mission, and a game begun at a later mission
  with all its score has reached. Found, an item's described in the next briefing
  ([briefing.md](briefing.md)) by its `Who Discovers` (Asunder for 41),
  its `Discovery Text` a line of its `Script` (`Discovery Items`). The
  maps find some themselves (`CDiscoverInventoryNextBriefingAction`, 5),
  and ask whether one's been found (`CIsDiscoveredAction`). The program
  shows no score.
- **What an officer has** (`0x5b7c90`, asked by the team selection,
  the team bar and the map): an item of their inventory not found yet
  isn't had at all; one found is had as its `Overridden By` if that's
  found, and so on: the Phaser as the Phaser Damage Increase once
  that's found. Of the 59 items, 19 start not found, 12 can be overridden.

The manager's win check (`0x50c9df`): with every required objective
complete, the others still incomplete are set Failed (`0x50ca2c`), and
the Win Action runs, once (`0x50ca56`).

### The debriefing

After a mission completed, one with `Show Debriefing` (`Debrief Script`
its lines, as the briefing's `Script`; `Debriefing Music`) shows
Rules.txt's `Debriefing Screen` (`CStarTrekDebriefingMenu`, made at
`0x48a780`), on its `Debriefing/Background`, with the bridge's rumble
(`Ambient/Bridge Rumble BG.wav`):

- **Three panels** fading in over 0.75 seconds each, from 0.75, 1.25 and
  1.75 seconds (`0x48a898`), `Interface/Mission Debrief Fade.ogg` as each
  starts (`0x4895ae`), and out again on leaving: each panel and what's on
  it drawn through the toolkit's brightness, in its six steps, from
  dark (`0x48977c`), not see-through.
- **The mission's**, `Mission Background` at `Success Pos` (22, 50)
  (`0x48a600`): "MISSION SUCCESSFUL" in `Placar Condensed`, "PRIMARY
  OBJECTIVES COMPLETE" and "TIME TAKEN: " in `Placar Blue`, each centred
  2 pixels left of the panel's middle at its row (`Mission Success Text`
  39, `Objective Complete Text` 100, `Time Text` 119), the time ending
  there and the mission's time, hours, minutes and seconds of two figures
  each (`0x5c4700`), 2 pixels right of it.
- **The objectives'**, `Objective Background` at `Objective Pos` (322,
  50), the list at `ObjectiveText Pos` and `Size` (`0x489630`).
- **The text's**, `Debriefing Text Background` at `TextDisplay Window
  Pos` (51, 237) (`0x4897b0`): the lines typed out as the briefing's,
  at `TextDisplay Text Speed` (60) in `Debriefing Text Font`, beside the
  speaker's portrait on its `Portrait Background`, with their
  voice-overs; CONTINUE and REPLAY on the panel, at their places on it.
  The text runs across the panel, `TextDisplay Text Pos`'s margin either
  side (`0x48a1a1`). A panel ending its fading in calls its slot `0x130`
  (`0x4895e9`); the text's sets the first line then, its voice-over
  with it, and marks itself ready (`0x489cd0`, `0x489e0a`); till then
  the lines don't go on (`0x48acf9`). A line typed goes on to the next by itself once its
  voice-over has been over half a second (`0x48ad4b`, 0.5 at
  `0x616fb0`), or at once without one; under the text, in its font, the
  prompt: "Press 'Space' to display all text" while typing, "Press
  'Space' to continue debriefing" with a line to come, and "Press
  'Continue' to end debriefing" after the last (`0x48ad64`), centred on
  the panel but its right end no further than 343 across it, and 6
  above its foot (`0x48a1eb`).
- **The sounds:** the rumble is the game's ambient sound, its loop bit
  set (`0x5ba010`, `0x5ba02e`), so it plays over and over; the music is
  queued as the mission's maps end ([maps.md](maps.md#the-music)).

## In Reluva

`loadMission` keeps the mission's objective manager, and
`readMissionObjectives` (`game/objectives.cpp`) reads it. `MapScreen`
(`ui/map_mission.cpp`) keeps the objectives, the mission's first and
then those added, and runs the actions above, a count's as above.
An objective completed runs its Success Action, failed its Fail Action;
with every objective that's required and not disabled complete, the
others still to do fail, without their Fail Actions, and the Win Action
runs, once. Each spawn point's Spawn Action runs as its officer's put
there, the officer the instigator and the point the trigger
(`CSpawnPointAI`, `0x415ca9`); a protect objective's `$instigator`
is that officer. Their own rules are as above: a protect or rescue
fails as their officer, the character of its name, or an officer's
stand-in off the team dies; a rescue's complete as they join the team,
or come back to it; a need-item objective fails as someone dies with no
one living on the team holding its item. A rescue's "%s" is their name
*(inferred)*. A part's Destroyed Action runs as an enemy is killed.

The objectives window (`ui/objectives_window.cpp`) opens from the
objectives button and O, listing the objectives shown; RESUME, Esc, or
the button or O again closes it (`0x476ab4`). The button does nothing
while another window has the map's input (`0x476ad3`). The End Game
Menu shows as a mission ends. Both open in the middle of the screen
(the game's window opening with mode 6, `0x4d5570`, from `0x476b0c`
and `0x50d434`). The objectives window holds the game still while it's
open: it counts itself on the program's holds (`0x51ad9b`, `0x4d53e0`),
and while any are counted the frame doesn't run the map (`0x4f645c`).
The End Game Menu takes the map's input away; the game goes on under
it, as above. CONTINUE goes to the mission the triggers named next,
from its briefing, or after the last, the `EndGame` movie and the main
menu (Reluva goes to the main menu after a mission naming none, which
no retail mission is); RESTART starts the mission's map again with the
same team; END GAME, pressed twice, goes to the main menu, the line
under the buttons as above. LOAD GAME opens the Load Game screen
([menus.md](menus.md#saved-games)).

The debriefing (`ui/debriefing.cpp`) comes between a mission completed
and the next, as above; its first line, with its voice-over, is set as
the text's panel ends its fading in, and nothing goes on before, as the
program's, and they go on by themselves half a second after their
voice-over, or at once without one; CONTINUE or Space shows a line
whole, then the next, and after the last goes on; REPLAY starts the
lines again. The text runs across the panel and its prompt is under
it, as above; the rumble plays over and over, and the music is the
menu's ([maps.md](maps.md#in-reluva)). The mission's time is counted while its map's played,
not while a window holds it.

Where to go is shown on the minimap by its points of interest
([maps.md](maps.md#the-interface)).

The score (`game/campaign.cpp`) is kept from mission to mission, and
saved: a mission completed adds its base and the Points of its
objectives done that aren't required. A new game begun at a later
mission starts with 100 for each before, and the items that's reached
found, described in its briefing *(inferred)*. Restarting a mission keeps
the score it had. Each item whose Points to Discover the score's reached is
found, described in the next briefing ([briefing.md](briefing.md)) with
those the maps found. The officers' inventories, in the team selection
and taken into the mission, are as the program has them: those not
found left out, those overridden by an item found as that
(`resolveInventories`, `game/team.cpp`), and the items a mission needs
likewise: so the team selection offers what's been found, the Phaser
Damage Increase in place of the Phaser once the score's 400.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; the missions (`.MDS`), maps and
  scripts in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, Mission 01:
  its three objectives and the four Protect objectives its spawn points
  add; the objectives window with its two lists; the two scientists
  counted and the band found, the Win Action beaming the team out and
  the mission completed with "Both scientists have been beamed aboard
  successfully.", Mission 02 next; an officer killed by the Romulans and
  the mission failed with "One of your team members has died."; the
  debriefing with its three panels, the time taken and Marcus Refelian's
  log. Not yet
  against the original running. Unit tests (`stawayteam.unit.maps`)
  read a mission's objectives, count one to complete, end the mission by
  its Win Action with its message, and fail a protect objective as its
  officer falls; `stawayteam.unit.menus` the debriefing's panels fading
  in one after another and its lines.

## Open questions

- **The windows:** what the Objectives Menu's `Placar Condensed` font
  is for.
- **Going on:** what the program does after a mission that names no
  next one (none does): it would make the mission its table's first
  (`0x5b7560` moves an empty choice in).
