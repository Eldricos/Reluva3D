# Star Trek: Away Team: team selection

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/team_selection.cpp`, `src/engines/stawayteam/game/team.cpp`, `src/engines/stawayteam/ui/static_text.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

Before a mission that has one, the player picks the away team: the
roster's officers, a field at a time, put on the transporter, as many as
the mission takes. The screen (`CStarTrekTeamSelectionMenu`, made at
`0x5ea240`) shows the officer chosen, the team on the transporter, the
mission and the items it needs. Its layout is `Rules.txt`'s `Team
Selection` menu; the officers, missions and transporters are files of
their own in `DATA.dat`.

## How it works

### The files

- **The roster:** `Character/Starting Roster/<id>.txt`, a
  `CTeamMemberInfo` each: `First Name`, `Last Name`, `Rank`, `Field`,
  `Portrait`, `Is Available`, `Character Script` and `Character Text`
  (where the description is), `Attribute` (`Name`, `Value`) and
  `Inventory` (`Inventory Definition`, `Count`; -1 for the retail
  officers' items). The file's name is the officer's id, as the missions
  name them ("Slovaak", "Marcus Refelian").
- **The portrait's definition:** `Character/Character Portraits/<Portrait>.TXT`,
  a `CStarTrekFrame`: `Race`, `Gender`, `Head Icon`, and the head and
  body models (`0x5e4d70`, `0x5e4e70`). The portrait is
  `Cache/Character/Character Portraits/<Portrait>.frm16`, the icon
  `Cache/Character/Character Icons/<Head Icon>.frm16`.
- **The fields:** `Character/Field/<field>.txt` (`CFieldDefinition`),
  their names the tabs' order *(inferred: the files' order is the
  pictures' Command, Engineering, Medical, Science, Security)*.
- **The mission:** `Levels/Mission NN/Mission NN.MDS`, a
  `CMissionDescription`: `Mission Name`, `Show Briefing`, `Show Team
  Selection` (0 for mission 1, the only one; it goes straight in),
  `Transporter Background`, `Max Members To Start`, `Who Should Start`
  (ids: they start on the transporter and can't be taken off) and
  `Items` (each needed by someone on the team).
- **The transporter:** `Transporter Backgrounds/<name>.txt`: its
  `Background Sequence` (`Cache/Sequences/<name>.seq16`), the places'
  `Position`s, and nernies: static texts ("Incursion Shuttle Ready") and
  random numbers.

### The screen

Everything at `Rules.txt`'s places, in the menu font, Trek Small, unless
said:

- **Its texts:** the definition's `Nernie` static texts ("AWAY TEAM
  COM", "TEAM ROSTER -"...), each at its `Position` in its `Font`.
- **The buttons** (`0x5ba0e0`, pictures `Team Selection/<name>`): EXIT
  (Exit), BRIEFING (Briefing), START MISSION (Continue; its label on its
  right, `0x5eac5d`, single player only), ADD MEMBER and REMOVE MEMBER
  (the arrows), INVENTORY and OFFICERS (Character), the last two tabs,
  OFFICERS chosen to start with. A disabled button shows its `<name>
  Disable` picture.
- **The officer** (`0x5e8620`) at `CharacterDisplay Pos`, on `Character
  Background`: the portrait in the middle of `Portrait Background` at
  `PortraitDisplay Pos`; at `InfoDisplay Pos`, a line each, "Name: ",
  "Rank: ", "Race: ", "Gender: ", "Field: " with the value after the
  label; at `AttributeDisplay Pos`, each attribute's `Attribute Bar` as
  long as its value (of 100 *(inferred)*, the bar at most `AttributeDisplay
  Width`), its name on it and its value at its end, `AttributeDisplay
  Spacing` between them; the description, the character script's entry
  (`Levels/<Character Script>.RDS`), wrapped to `CharacterDescription
  Size` at `CharacterDescription Pos`, 2 pixels between lines. The
  officer shown is the one last clicked, in the roster or on the
  transporter; none to start with.
- **The roster** (`0x5e8070`, `0x54f2d0`) at `RosterDisplay Pos`, on
  `Roster Window/Team Roster Background`: a tab for each field
  (`Roster Window/<field>`, `Class Button Spacing` apart, the first
  chosen; each plays `Interface/Roster Window/<field> Tab.wav`); the
  field's available officers not on the team as icons, `Roster Icon
  Width` a row, `Roster Icon Y Spacing` apart, from `RosterIcon Pos` and
  `RosterIconOffset Pos`, each on `Icon Background` (`Icon Select
  Background` for the one chosen); the name of the officer under the
  mouse, or chosen, centred at `RosterTitle Pos`; the field's name ending
  at the third column, over the second row.
- **The transporter** (`0x5e6ba0`, drawn at `0x5e7a60`) at `TeamDisplay
  Pos`: its background sequence by its frames' origin at `TeamTransporter
  Pos`, its texts; the selection marker (`Cache/Models/Interface/Selection.mdl16`,
  `Idle`) at each place the mission allows, plus `TeamSelectionMemberOffset
  Pos` and `TeamSelectionMemberSelectOffset Pos`, at half brightness but
  the chosen place's; each officer's figure
  (`Cache/Sequences/Characters/<name>.seq16`) at their place plus
  `TeamSelectionMemberOffset Pos`, the team drawn from the last; the
  name of the officer under the mouse, or chosen, centred at `Team
  Selection Title Pos`.
- **The mission** (`0x5e9f60`) at `MissionName Pos`: "Mission NN" over
  its name, each centred in `MissionName Size`. At `Skill Pos`
  (`0x5e91e0`), "Required Items for Mission" and the items, those no one
  on the team has in Trek Small Red with " - NEEDED" *(inferred: the
  layout isn't traced)*.
- **A message** at `Message Pos`, centred in `Message Size`, for 50
  (`0x5ebc1f`; seconds *(inferred)*).

### What the buttons do

From the screen's handler (`0x5eb930`):

- **ADD MEMBER** puts the roster's chosen officer on the next place, if
  the mission takes more.
- **REMOVE MEMBER** takes the chosen place's officer off, unless the
  mission says they start: then *"<name> is a required team member for
  this mission, and cannot be removed."*
- **START MISSION** with no one: *"Select at least one team member before
  continuing."*; with an item no one has: *"The selected team is missing
  one or more required items.  Click on a RED required item to cycle
  through the team members with that item."*; else the mission starts.
- **EXIT** asks *"Are you sure you want to quit the game and exit to the
  main menu?"*.
- **INVENTORY** and **OFFICERS** show the inventory's panel (`Inventory
  Background` at `Inventory Pos`) or the officer's.
- **BRIEFING** goes back to the briefing ([briefing.md](briefing.md)).

## In Reluva

`TeamSelectionScreen` (`ui/team_selection.cpp`), its files read by
`loadRoster`, `loadFields` and `loadMission` (`game/team.cpp`), the
static texts by `readStaticTexts` (`ui/static_text.cpp`). START GAME in
the difficulty window starts a new game at mission 1: its briefing, and
with no team selection for it, nothing more yet. In a build with developer
keys, the engine's "Developer: first mission" setting starts it at
another, so the screen can be reached. START MISSION and EXIT come back
to the main menu; there are no missions to play yet. Not there yet: the
inventory panel's contents, clicking a red item, the
transporter's random numbers, and the second figure the program draws
from each officer's body model.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`, the roster, missions
  and transporters in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files at mission 2,
  the screen looked at: the panels, buttons and texts at their places,
  Slovaak and Marcus Refelian on the shuttle's transporter, an officer
  clicked in the roster shown with portrait, details, attributes and
  description, ADD MEMBER putting her on the next place, REMOVE MEMBER
  refusing Slovaak with its message, the inventory tab. Not yet against
  the original running. Unit tests (`stawayteam.unit.menus`) read a
  roster, fields and a mission made in the test, and check the team to
  start with and START MISSION's rules.

## Open questions

- **The officer's name over the transporter** falls on the shuttle's
  "Incursion Shuttle Ready" text where `Rules.txt` places it.
- **The attributes' greatest value**, the bar's whole length. Reluva
  takes 100.
- **The required items' panel:** its layout and colours.
- **A message's time:** Reluva takes the 50 as seconds.
- **The figures' frames:** Reluva starts each officer's two frames on
  from the one before.
- **Where a place answers a click:** Reluva takes about the figure.
