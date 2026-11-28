# Star Trek: Starfleet Command II: settings files (`.gf`, `.mct`)

> **Status:** decoded · **Code:** `src/engines/sfc/formats/settings.cpp` · **Checked on:** English retail 1.0 (40 files) and 2.0.3.6 (48) · **Confidence:** confirmed unless marked

## Overview

The game's settings are text in one syntax:

- **`.gf`** files: the client's and server's settings (`ClientSettings.gf`,
  `ServerSettings.gf`, `CommonSettings.gf`), the Dynaverse's
  (`MetaAssets/ServerProfiles/Singleplayer/`: its clock, economy,
  scoring, mission matching, characters, ships, news), help topics, and
  the ships' names by empire (`Assets/CommonSettings/ShipNames.gf`).
- **`.mct`**, one a campaign (`Assets/Scripts/Campaigns/`): its name and
  description as New Campaign shows them, its three eras' maps
  (`EarlyMapName` and the like), its difficulty and era, the first
  mission of its story and the prestige that brings it on
  (`TriggerMission`, `TriggerPrestige`: Peace In Our Times was offered
  from 800, [hosting.md](../systems/hosting.md#its-missions)), and its missions' scripts by number (`[Missions]`).
- **A campaign map's header** ([mvm.md](mvm.md)).

## Layout

8-bit text (Windows-1252), lines ending CR LF:

- `[name]` starts a section; names are paths, `/` between their parts
  (`[Clock/StartingDate]`). Lines before the first section belong to an
  unnamed one (a `.mct`'s name and description, a `.gf`'s `Name`).
- `key = value`, the spaces around both not part of them. A value in
  double quotes is what's between them, `//` and spaces included;
  otherwise a `//` after it starts a comment (`MilliSecondsPerTurn =
  60000 // (60000)`).
- `//` at a line's start makes it a comment; blank lines don't count.
- Keys are numbers for lists (`0="Met_1Scout.scr"`), names otherwise.

Two retail values have no closing quote (`ShipNames.gf`'s `"RIS Sui
Iuris`, `Character.gf`'s `"Habablathh`); the reader takes them to the
line's end, as the game presumably does *(inferred)*.

## In Reluva

- **Reading it:** `SettingsFile` (`src/engines/sfc/formats/settings.h`):
  the sections in order, each key's value and whether it was quoted;
  values found by section and key without case.
- **Commands:** `reluva-tool sfc settings <game folder>` reads every
  `.gf` and `.mct`, counting their sections and keys.
- **The data viewer:** the Settings page lists each `.gf` and `.mct`
  file's sections and keys.
- **Not yet:** the game's use of them is its own program's, hosted;
  nothing in Reluva acts on a value yet.

## Sources and checks

- **Where the facts come from:** the files themselves: all 40 of 1.0
  (189 sections, 1,896 keys, 1,298 of them quoted, 69 with a comment
  after the value) follow the rules above, with nothing else on any
  line.
- **How it was checked:** every file read without an error, 1.0's and
  2.0.3.6's (48 files, 2,221 keys).
- **Unit test:** `sfc.unit.formats` (settings): a preamble, comments,
  quotes keeping `//`, sections by path, a quote not closed, and the
  errors (a section without `]`, a line that's neither).

## Open questions

- Whether the game reads a quote not closed as the reader does.
- What each key does: worked out one by one as the hosted game is
  followed, not from the files alone.
