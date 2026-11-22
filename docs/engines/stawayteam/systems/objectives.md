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
  "Rescue %s.". The map's actions complete them.

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
- **`CSetMissionObjectiveCountAction`** (158), its `Count` always 1.
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

### The debriefing

After a mission completed, one with `Show Debriefing` (`Debrief Script`
its lines, as the briefing's `Script`; `Debriefing Music`) shows
Rules.txt's `Debriefing Screen` (`CStarTrekDebriefingMenu`, made at
`0x48a780`), on its `Debriefing/Background`, with the bridge's rumble
(`Ambient/Bridge Rumble BG.wav`):

- **Three panels** fading in over 0.75 seconds each, from 0.75, 1.25 and
  1.75 seconds (`0x48a898`), `Interface/Mission Debrief Fade.ogg` as each
  starts (`0x4895ae`), and out again on leaving.
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

## In Reluva

`loadMission` keeps the mission's objective manager, and
`readMissionObjectives` (`game/objectives.cpp`) reads it. `MapScreen`
(`ui/map_mission.cpp`) keeps the objectives, the mission's first and
then those added, and runs the actions above. A count's Count is added,
and at its Count Before Success the objective is complete *(inferred)*.
An objective completed runs its Success Action, failed its Fail Action;
with every objective that's required and not disabled complete, the Win
Action runs, once *(inferred)*. Each
spawn point's Spawn Action runs as the map starts, its officer the
instigator *(inferred)*; a protect objective's `$instigator` is that
officer, and it fails as they fall *(inferred)*. A rescue fails likewise
as its officer falls, or as the character of its name is killed, till
they've joined the team; its "%s" is their name *(inferred)*. A part's
Destroyed Action runs as an enemy is killed.

The objectives window (`ui/objectives_window.cpp`) opens from the
objectives button and O, listing the objectives shown; RESUME or Esc
closes it. The End Game Menu shows as a mission ends. Both are in the
middle of the screen *(inferred)*, and hold the game still while they're
open *(inferred)*. CONTINUE goes to the mission the triggers named next,
from its briefing, or to the main menu without one *(inferred)*; RESTART
starts the mission's map again with the same team; END GAME goes to the
main menu. LOAD GAME is greyed out, as saved games can't be loaded yet.

The debriefing (`ui/debriefing.cpp`) comes between a mission completed
and the next, as above; its lines are typed once their panel's in, and,
as the briefing's, go on by themselves a second after their voice-over
*(inferred)*; CONTINUE or Space shows a line whole, then the next, and
after the last goes on; REPLAY starts the lines again. The text stops
short of the buttons *(inferred)*, and the music and rumble play over and
over *(inferred)*. The mission's time is counted while its map's played,
not while a window holds it.

Where to go is shown on the minimap by its points of interest
([maps.md](maps.md#the-interface)).

Not there yet: the score and points, and need-item objectives' own
rules.

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

- **The manager:** when it runs the Win Action, whether a failed
  required objective ends the mission by itself, and how the score's
  reckoned.
- **Rescue and need-item objectives:** whether the program completes or
  fails them itself, or only the maps' actions do.
- **The windows:** where they're placed, whether the game's held still
  under them, and what the Objectives Menu's `Placar Condensed` font is
  for.
- **Going on:** what's done without a next mission, and the
  debriefing's lines going on by themselves.
