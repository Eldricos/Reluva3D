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
  (ids: they start on the transporter and can't be taken off), `Items`
  (each needed by someone on the team) and `Recommended Items`.
- **The items:** `Inventory/Item Definitions/<name>.txt`, a
  `CStarTrekInventoryItemDefinition` each, named by the file as the
  officers and missions name them: `Printable Item Name` (the name shown;
  `Terminal Repair` is "Engineering Tech Kit"), `Icon Image`
  (`Cache/Inventory/Icon Frames/<name>.frm16`), `Inventory Type`, `Display
  In Inventory`, and `Briefing Script` and `Briefing Text` (where its
  description is, `Inventory Item Descriptions`). The types are
  `Inventory/Item Types/<type>.txt`: Gadget, Offensive Gadget, Primary,
  Secondary, Skill and Special, and `!None`.
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
  long as its value of 100 (an attribute's most, `0x5e5830`), the bar at
  most `AttributeDisplay Width` and the picture's own width (`0x5e8b96`),
  its name on it and its value at its end, `AttributeDisplay
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
  the chosen place's; each officer's figure at their place plus
  `TeamSelectionMemberOffset Pos`, the team drawn from the last: their
  body model's sequence (`Cache/Sequences/Characters/Generic Male
  Body.seq16`), and over it their own, the head
  (`Cache/Sequences/Characters/<name>.seq16`), a frame of each together,
  at the head's rate plus 2k / n less 0.1 frames a second for the kth of
  n, so they don't move as one (`0x5e7870`), each drawn through its
  sequence's render effect (`0x55ba70`): for all 22 of the characters'
  sequences, `Transparent Mask`, the default draw (`0x71d1f8`, registered
  at `0x45dc00`), its see-through colour left out; the
  name of the officer under the mouse, or chosen, centred at `Team
  Selection Title Pos`.
- **The inventory** (`0x5ea5b7`, drawn at `0x5eb2c0`) in place of the
  officer's panel, on `Inventory Background` at `Inventory Pos`: the
  officer shown's items, a column for each item type, left to right in
  the types' order (`0x5e6b80`, by their index: their files' names
  sorted ignoring case, `0x41fa91`), `Inventory Icon Size` and `Inventory Icon Spacing`
  apart from `Inventory Strip Pos`; in a column its items from the strip
  upward (`0x423040`), each over a black square of the icon's size and
  in the middle of `Roster Window/Item Frame` (`0x423120`); the column's
  `Selected Icon Background` goes only under its own chosen item
  (`0x42330c`), which only the game's inventory sets (`0x423da0`), so
  here none, and the mouse lights nothing. An empty column is the
  icon's size filled with (10,10,40) at alpha 128 (`0x423059`), blended
  as the fill does ([menus.md](menus.md)). As many as there are, without scrolling (Rules.txt's
  `Inventory Previous` and `Next Button Pos` are 0,0 and read by
  nothing). Only items with `Display In Inventory`, and only those the
  officer has as the game's found items have it: not found left out,
  overridden by one found shown as that
  ([objectives.md](objectives.md#the-score), `0x5ec1e0`). The
  item under the mouse is named, and *" (Double-Click for description)"*,
  centred under the strip (`0x5eaed0`, `0x5eb345`).
- **An item's description** (`0x5e8d60`), on a double click: `Inventory
  Description Background` in the middle of the screen, the item's name
  centred at `Description Pos` (with `Description Margin`), two lines
  under it the icon on `Inventory Description Icon Background`, and its
  description typed out beside and under it at 90 characters a second
  (`0x5e8ec0`); CONTINUE (`Inventory Done`, at
  `InventoryDescriptionExitButton Pos`) closes it, and is its default and
  cancel button, so Enter and Esc do too (`0x5e8e55`).
- **The mission** (`0x5e9f60`) at `MissionName Pos`: "Mission NN" over
  its name, each centred in `MissionName Size`.
- **The items it needs** (`0x5e91e0`) at `Skill Pos`: two lists, each
  its title in Trek Small Gold centred across `Skill Size`, then its
  items a line each, a pixel between lines (`0x5e9620`): *"Required Items
  for Mission"*, its items, or *"NONE"*; under it *"Recommended Items"*,
  left out when there are none. An item no one on the team has is in
  Trek Small Red, a required one with *"NEEDED"* at the line's right. A
  band of dark red, its red 64 + 16 sin(pi t) of 255, pulsing every 2
  seconds, is behind those the officer shown has (`0x5e9781`), the items
  compared as had (`0x5ebf7b`). Clicking an
  item's line goes on to the next available officer, after the one shown,
  who has it, choosing them on the transporter or in the roster, with
  `Interface/Generic Beep.wav` (`0x5e9ad9`, `0x5ebf10`).
- **A message** at `Message Pos`, centred in `Message Size`, for 50
  seconds (`0x5ebc1f`), counted down by the windows' frame time
  (`0x5e9f2a`).

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
static texts by `readStaticTexts` (`ui/static_text.cpp`). It shows
before each mission that has one, after the briefing; START MISSION
starts the mission with the team chosen, and EXIT, asked first, goes
back to the main menu. In a build with developer keys, the engine's
"Developer: first mission" setting starts a new game at a later
mission. The items are read by `loadItems` and `loadItemTypes`
(`game/team.cpp`), the officers' inventories and the mission's items as
had by `resolveInventories` and `heldAs`. The figures' time is the
screen's own, from 0 as it's first drawn, as the program's starts as
the screen's made (`0x5e6c65`) and goes on with its updates
(`0x5e6e70`). A place answers a click or the mouse where its officer's
figure, body or head as drawn then, isn't see-through (`0x5e71b0`, the
pictures' own test, slot `0xc8`, taken as their pixels'); an empty
place doesn't. The
transporter's random numbers roll as the briefing's
([briefing.md](briefing.md#the-screen)).

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`, the roster, missions
  and transporters in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files at mission 2,
  the screen looked at: the panels, buttons and texts at their places,
  Slovaak and Marcus Refelian on the shuttle's transporter, an officer
  clicked in the roster shown with portrait, details, attributes and
  description, ADD MEMBER putting her on the next place, REMOVE MEMBER
  refusing Slovaak with its message; the inventory tab with Slovaak's
  items, one named under the mouse and described on a double click; the
  required and recommended items, a click on one moving on to the next
  officer who has it; at a new game's start the 19 items not found left
  out of the roster's inventories, and Slovaak and Marcus Refelian drawn
  whole, body and head, where only their heads were. Not yet against
  the original running. Unit tests (`stawayteam.unit.menus`) read a
  roster, fields, items and a mission made in the test, and check the
  team to start with and START MISSION's rules, and an officer's items
  as had, found and overridden.

## Open questions

- **The officer's name over the transporter** falls on the shuttle's
  "Incursion Shuttle Ready" text where `Rules.txt` places it.
- **A picture's own test:** whether slot `0xc8` takes a pixel as there
  by its colour key, as Reluva takes it.
