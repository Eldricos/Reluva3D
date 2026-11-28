# Star Trek: Starfleet Command II: a battle in memory, and the pilot

> **Status:** partial · **Code:** `src/engines/sfc/host/battle.cpp` (reading it), `src/engines/sfc/host/pilot.cpp` (flying the player's ship), `src/engines/sfc/tools/host.cpp` (`-survey`, `-pilot`) · **Checked against:** English retail 1.0, in Reluva's x86 emulator: campaign battles surveyed, and the readings checked against the HUD · **Confidence:** from the code where marked, seen otherwise

## Overview

A mission's battle is kept in the program's memory as objects by
number: the ships, the planets and bases, and the smaller things around
them. Reading them lets a test see what the game sees, and the pilot
uses that to fly the player's ship by the game's own keys and clicks,
for testing missions headless. Inputs fixed in advance can't answer an
enemy: they win easy fights and lose the rest
([hosting.md](hosting.md#its-missions)). This page says where the
battle is kept and how the pilot plays it.

## How it works

### The world and its objects

- **The world** is at 0x83fea0, the object the battle's code works on.
  The player's object's number is at +0x24 (0x422404), and its objects,
  by number, are pointers from +0x30; it takes numbers below 0x4000
  (0x422172) *(from the code)*.
- **Ships** are numbered from 1024 in the battles seen, each object
  starting with the ships' class's table of methods, 0x71bd3c. The
  others below 1024 (0x71b664, 0x73072c, 0x73080c) aren't ships, and
  sit at 0,0 but for two *(seen, not traced)*.
- **A ship's object:**

  | Offset | What |
  |---|---|
  | +0x18 | Flags: bit 0 set on what moves, 3 on ships, 0xb on Organia *(seen, not traced)* |
  | +0x1c | Its own number |
  | +0x28 | The number of the object it has targeted, -1 for none; the player's own is the HUD's target, changed by the targeting keys *(seen, not traced)* |
  | +0x34, +0x38, +0x3c | Where it is (floats), as 0x4221ab takes it *(from the code)*; +0x40 on, where it was a moment before *(seen, not traced)* |
  | +0x60 | Its slot among the mission's teams, as 0x4a7b2c takes it *(from the code)* |
  | +0x98, +0xa0 | The way it faces, a unit vector in the plane, the way it moves *(seen, not traced)* |
  | +0x13c | Its name ("USS Berlin", "IKV Glory", "Organia") |

- **Range** as the HUD shows it is a tenth of the distance between two
  objects' places (0x71be90, 0.1) *(from the code)*.
- **Sides:** the HUD's "Allied" over a target comes from the target's
  slot (+0x60) and a table of how the teams stand (0x4a7b43). The teams
  object (pointer at 0x8872d4) holds the player's slot at +0x6200
  (0x4bf781); the table (pointer at 0x86a758) has a row for each of 20
  slots from +0x54, each row's entries from +0x10, and a slot stands 1
  to itself (0x424fbf) *(from the code)*. The values seen: 1 the
  player's, 2 allied (a convoy), 5 neutral (the peace conference's
  delegates in Peace In Our Times), 6 enemy (raiders, the ISC).

### The pilot

Each frame it reads the battle and gives one input, a key or a click
(the game drops presses that come together: six presses of S in one
frame left the speed at 1):

- At the start of a battle, and whenever the game moves the player to
  another ship when theirs is lost, red alert, the speed raised, and the
  game's nearest enemy chosen (the backquote). It chooses again when its
  target isn't an enemy, and every 120 frames.
- It steers by clicking the 3D view left or right of its middle (the
  camera sits behind the ship), toward the target, or away from it
  inside range 3 until it's past range 10.
- It reinforces the shield facing the target (the keypad: 8 forward, 9,
  3, 2, 1, 7 round to the right) and fires every 30 frames with the
  target inside range 8 and within 60 degrees of the bow.

What it does, played:

| Battle | Fixed inputs | The pilot |
|---|---|---|
| Convoy Escort, the frigate alone | Won each time (Astounding Victory) | Lost: the frigate destroyed at 4:18 |
| Convoy Escort, a frigate and a destroyer | Lost three times in four | Lost each version; once both raiders destroyed (enemy losses 188) before the frigate went, the destroyer flown on |
| Peace In Our Times, after the delivery | Lost | Lost: the ISC destroy the frigate before a hostile shows |

So it finds, tracks and fights the enemy as the game counts it, but
fights worse than "follow the nearest enemy and fire every 60 frames":
its turning away and its narrower firing cost more than they save.

## In Reluva

- `reluva-tool sfc host <game> ... -survey <frame>` lists the world's
  objects at a frame: number, address, class, flags, place, slot,
  relation, and each one's first 1 KB as words, to tell fields apart.
- `-pilot <frame>` lets the pilot fly from that frame on, logging what
  it sees and chooses ("pilot: ship 1024, target 1026 at range 23
  bearing 30, closing, 3 hostile").
- `sfc.unit.battle` lays a battle out in memory as the game does and
  checks the reading and the pilot's first inputs.

## Sources and checks

- **Where the facts come from:** the program's code where marked, by
  following the HUD's target panel (0x4983f5: the range and speed it
  prints) and its "Allied" label (0x4a7b7d) back to the data they read;
  the rest from surveys of campaign battles, compared object by object.
- **Checked:** the relations against the HUD's labels and the debriefing
  (the convoy allied, raiders enemy); the heading against each ship's
  movement; the bearing's sign against the screen (a target the pilot
  put 30 degrees to the right showed right of the middle); the range
  against the HUD's.
- **Not checked:** the meaning of the classes below 1024, and the
  relations other than 1, 2, 5 and 6.

## Open questions

- What a ship's hull, shields and weapons' charge are in its object:
  the pilot can't yet tell when to break off, or when its weapons are
  ready.
- How the AI's ships pick their tactics: a pilot that used the game's
  own follow and fire would likely do better than one that turns away.
- Which speeds and ranges each of a ship's weapons need, from
  `shiplist.txt`, to fire as they bear.
