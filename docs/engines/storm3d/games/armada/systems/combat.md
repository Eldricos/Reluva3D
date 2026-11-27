# Star Trek: Armada: combat

> **Status:** partial · **Code:** `src/engines/storm3d/play/combat.cpp`, `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, its draws every second as its starbase fought one of its own construction ships · **Confidence:** seen, not traced, but a hit's course and the special weapons from the code

## Overview

A unit with weapons attacks what it's told to: its attack button, then
a click on the target. Each weapon fires on its own `shotDelay` while
the target is in its `range`; each shot flies at its ordnance's
`shotSpeed` and does its `damageBase`, give or take its
`damageVariance`. Damage comes off the shields, a unit's `maxHealth`,
which come back at its `shieldRate`; what they can't take lands on a
system, on the hull, costing crew, or critically, destroying the unit.
There's no hull of points: a unit is destroyed only by a critical hit,
and one without crew is left derelict.

## How it works

### Watching the original

The skirmish's sides start far apart, but the original lets a unit
attack its own: its starbase (`fbase`) was told to attack a
construction ship (`fconst`) 60 from it, and the host wrote its draws
every second from then to 140 seconds.

- **The starbase's weapons:** eight `fbpulse` (`weapon1` to `weapon8`):
  range 400, a shot each 4.5 seconds, its ordnance `fbpulseo` 22 give or
  take 2, at 300 a second, its sprite `pphaser` (50 long, 8 across).
  All eight fired together, from the order on.
- **The shields' bar over the target** (`large_shield_bar`, as the
  selection's health bar, 20 across over this ship) fell 4.94 at each
  volley, a quarter, and came back 0.1 a second between them. Eight 22s
  are 176, a quarter of 700, the ship's `maxHealth`; 0.1 of 20 a second
  of 700 is 3.5, its `shieldRate`: its shields are its `maxHealth`.
- **The bar's colour** went with it from (0, 128, 0) full to (128, 0, 0)
  empty: (31, 96, 0) at three quarters. On the info panel the selected
  unit's shield bar went the same way from (0, 255, 0) to (255, 0, 0).
- **The hull:** the shields were gone at the fifth volley, 18 seconds in;
  the ship lasted the sixth and not the seventh, 27 seconds in, which
  the code below doesn't account for: it has no hull of points.
- **Destroyed,** it left debris and its officers went back to the count
  (4 to 2 of 120); the selection lost it.

### Crew and systems

The construction ship's crew, on the info panel once it was selected at
67 seconds: 390 of 500 after five volleys, 235 after the sixth, the
first all on its hull (155 at once), then 229, 224, 218 and 213 a
second apart, about 5.5 a second between volleys. All five of its
system icons were red from then, and it burned.

From the definitions: a shot's ordnance has `shieldCrewModifier` (0.2
for the starbase's) and `hullCrewModifier` (0.3); a unit has five
systems, each with `<system>HitPoints` (10 each for a construction
ship), `<system>HitPercent` (the share of the hits it takes: shields
7.5, engines 2, weapons 10, life support 2.5, sensors 7.5; the hull 70,
critical 0.5), `<system>RepairTime` and `<system>CrewLoss`, and
`lifeSupportLoss` (2). `ART_CFG.h` has the debris: the "chunk manager",
`ST3D_CHUNK_DENSITY` 0.8, pieces flying at up to 70 and turning at up
to 4 a second, for up to 2.75 seconds, as the original's shards were
gone within two.

From the code *(from the code)*:

- **A hit** (0x461c60): all its damage on the shields (`RTS_CFG.h`'s
  `SHIELD_DAMAGE_PROTECTION`, 1, the share; the program's own 0.8 if it
  isn't set); while they're up, what passes the ordnance's
  `damageThreshold` costs that much times its `shieldCrewModifier` of
  the crew in percent, and a whole one more at most. Only what the
  shields couldn't take goes on, to where the roll (below) puts it:
  - **a system**, losing whole hit points, what's under one kept for the
    next hit; brought to nothing, or hit while out, its `CrewLoss` percent
    of the crew and one more;
  - **the hull alone**, no points, only crew: the damage times the
    ordnance's `hullCrewModifier`, in percent, and one more at most;
  - **critically**: destroyed, if a whole point got through.
  The shields at nothing are kept at nothing: they don't destroy a
  unit, as one set to nothing in the original's memory showed.
- **Under one crew** (0x463410): none, and the unit derelict (its
  +0x4dc): its systems off. It isn't destroyed, and it doesn't keep its
  player in the game ([play.md](play.md#a-skirmish-lost)).
- **The roll** (0x461930): a whole number from 0 to 99, less each
  system's `HitPercent` in turn, engines, life support, weapons, the
  shield generator, sensors, then the hull's; it lands where it first
  goes below nothing, and past them all it's critical. The definitions'
  `criticalHitPercent` isn't read for it: the critical's share is what's
  left of the hundred, 15.5 for the Defiant (its others 84.5), none for
  the construction ship (99.5, the roll whole). Which system is which
  in a unit, by their shares and crew losses in the original's memory
  (2 and 5 the engines', 2.5 and 3 life support's): engines at +0x518,
  life support +0x588, weapons +0x550, the shield generator +0x4e0 (its
  loss drops the shields), sensors +0x5c0.
- **A shot landing** (0x449d60) does its ordnance's `damageValue` and
  `damageVariance` times a random number from -1 to 1 (0x4fa4c0: the
  game's own generator, its seed times 2,273 and 31 added, modulo
  0x19981595, the remainder of 100,001 less 50,000 over 50,000), times
  a factor its caller gives, carrying the ordnance's `damageThreshold`
  and crew and hull modifiers with it. One system is picked for it
  (0x461930): a roll from 0 to 99 run down each system's share in turn.
- **A weapon's chance** is its `hitChance` changed by six modifiers, the
  definitions' own comments say ("Modifier that changes the percent
  chance (from hitChance)"): the shooter's `stopToHitModifier`,
  `slowToHitModifier` and `warpToHitModifier`, and the target's
  `toHitStopModifier`, `toHitSlowModifier` and `toHitWarpModifier`.
  The pulses' and torpedoes' base definitions (`pulse.odf`,
  `photon.odf`) hit 0.7 of the time, with 0.5 more at a target stopped
  and 0.1 at one slow; the phasers' (`phaser.odf`) all of the time.
- **The debris** is the model's faces, each a piece of its own
  (0x572a40, the chunk manager): face by face, the density, 0.8, is
  added up, and each time the sum comes past 1, a face is broken off
  and 1 taken away, about four faces in five, starting from nothing
  again at each of the model's meshes; past
  `ST3D_CHUNK_MANAGER_MAX_CHUNKS` (1,500), the oldest go.

### A fight with the computer

The Defiant from the chase (`fdestroy`: 150 `maxHealth`, 80 crew, two
`dpulse`) was sent by the minimap to the computer player's base, 5,000
off. It came under the computer's starbase's fire at 236 seconds and
was gone by 248, having fired back from about 200 off, within its
pulses' reach, without circling; the shield glowed where its shots
struck. Its memory at 243 seconds: 13.5 of its 150 shields, 79.4 of its
80 crew, its five systems whole at 10 hit points each, and its height
38.5. A ship's crew so falls little while its shields last, the
starbase's pulses (22 against a `damageThreshold` of 23) passing the
threshold only now and then, as the code has it; that the construction
ship lost 110 of its 500 while its shields lasted, above, isn't
explained by it.

The systems in memory: engines (`enginesHitPercent` 2,
`enginesCrewLoss` 5), life support (2.5, 3), weapons (10, 2), and the
shield generator's and sensors' (7.5, 2 each); then the hull's 55 and
the critical's 15.5. The Defiant's end, between frames with its shields
nearly gone, fits a critical hit, the 15.5 of the hundred the code
gives it.

### Boarding, capture and repair

From the code *(from the code)*:

- **The transporters** (0x463630, 0x463710): a ship with more than
  `TRANSPORTER_MAX` (5) crew can start. Each `TRANSPORTER_DELAY` (1
  second), while its target is within `TRANSPORTER_RANGE` (500), it
  sends a party of `TRANSPORTER_MAX` crew: to another player's only
  while that unit's shields are at 1% of their most or less (else it
  stops), the party landing `TRANSPORTER_ACTION_DELAY` (1 second) later;
  to its own player's at once, while that one is short of crew. It
  stops once a party would leave it under its race's `crewRetreatRatio`
  of its most crew (0.1 for the Federation).
- **A party landing** (0x555400): on a derelict, its crew aboard and the
  unit its player's; on a unit of its own player, its crew added; on
  another's, a fight: the party's crew times its race's
  `boardingStrength` against the crew's times theirs, the bigger side
  keeping the difference over its strength in crew. The defenders
  beaten, the unit is the party's player's, its survivors its crew; a
  tie leaves it derelict.

From the files: the order buttons' tooltips (`Dynamic_Localized_Strings.h`)
say what they do. Transport, the rail's last button, "sends a boarding
party over to another vessel. On enemy vessels, the boarding party will
fight the crew and take the ship over if no crew remains. It will
reinforce crew on friendly vessels." Recrew, a station's, reinforces
it "with crew taken from the crew pool"; Repair and Recrew sends a ship
"to the nearest shipyard for faster repair and recrewing from the crew
pool". `RTS_CFG.h` has the transporters: `TRANSPORTER_RANGE` 500,
`TRANSPORTER_DELAY` and `TRANSPORTER_ACTION_DELAY` 1 second,
`TRANSPORTER_MAX` 5. Each race's definition (`fed.odf`, `kling.odf`,
`rom.odf`, `borg.odf`) has its crews' `boardingStrength` (the
Federation's 1.0, the Klingons' 1.5, the Borg's 1.75), `repairStrength`
(1.5, 0.75...), `crewYellowStatus` and `crewRedStatus` (the shares of a
crew its display warns at) with `repairYellow` and `repairRed`, a
`crewRetreatRatio`, and `crewAccumulationRate` 3.5: the crew a starbase
adds a second, as the original's counter showed
([play.md](play.md)). A unit starts with its `crewCost` aboard, of its
`maximumCrew`: the starbase's 800 of 1500, as the original's display.

### Chasing

A shipyard built, and a destroyer (`fdestroy`, its `dpulse` reaching
250) from it, the destroyer was told to attack a construction ship 186
away (A, then a click on the ship). It fired from where it was. The
ship was then flown 300 away: the destroyer moved 93 towards it,
stopped 246 from it, just inside its reach, and fired again from
there, the ship's crew falling (492 to 479 over 30 seconds). Their
places were measured on the frames against the shipyard's site, to
about ten units.

### Research and special weapons

From the definitions:

- **A weapon with `special` 1** is fired by its own button on the rail
  over the display (`b_<weapon>`), not at what the unit attacks. 67 are
  so, of 15 kinds (`classLabel`): cannons, launchers and area weapons,
  mine layers, cloaks and cloak detectors, beams that hold or tow
  (`toggleweapon`), wormholes, self-destruct, regeneration and more.
- **Special energy:** a unit has `maxSpecialEnergy`, coming back at its
  `specialEnergyRate` (the Defiant's 1000, 10 a second); a special
  weapon's ordnance costs its `specialEnergyCost` (an Antimatter Mine
  all 1000; the cloaks keep theirs on the weapon). The info panel shows
  it as `infoSingleEnergyBar`, in `infoSingleEnergyBarColor`, yellow,
  over its `DisableColor`: the Defiant's five yellow pips on the
  original's frames.
- **Research:** a research station's build items are pods (`fresear`'s
  `fedpod1` to `5`), each built as a ship is (Antimatter Mines' 40
  seconds, 200 dilithium, 10 officers), each giving one ship class's
  special weapon. Nothing in the files names which: the pod's tooltip
  is the weapon's without its key ("Antimatter Mines - Defiant", the
  weapon's "Antimatter Mines (^F^1) - Defiant"), so for all 39 pods but
  the five Tachyon Detection Grids, whose weapons say "Detect Cloak"
  *(inferred)*. Before research, the original's Defiant had its mines'
  button dark.
- **Researching, seen:** a Research Facility built, its menu had four
  pods lit and Corbomite Reflector (`fedpod1`) dark. Antimatter Mines
  ordered took 200 dilithium and 10 officers at once (24 to 34), was
  built as a model on one of the facility's arms, growing, and done 40
  seconds on, its button in the menu was dark: a pod is built once.
- **Antimatter Mines** (`gamine`, its ordnance `amine`, an `antimine`):
  a mine left where the ship is, seeing ships within its `detectRadius`
  (250), going for them at its `shotSpeed` (45), for 30 seconds
  (`lifeSpan`), doing 50 give or take 5; drawn as `wantmine.sod`.

### What's drawn

From the same run's frames and draws:

- **The shields' glow:** where shots hit the shields, a sheet of light
  stands off the hull on the shooter's side, about as long as the ship:
  784 triangles (a sphere's, 28 round by 14 *(inferred)*), added on,
  with the texture `Xshldx01` (a name of the program's own): a 4 × 4
  flip-book of a ripple spreading. 0.4 seconds after a hit it was at its
  eighth frame. Its colour goes with the shields as the info panel's bar:
  green full, (215, 40, 0) at 59 seconds, a sixth left.
- **Burning:** from 69 seconds, after the sixth volley, small fires
  burned on the ship: its models' damage emitters ([sod.md](../formats/sod.md)).
  Between 65 and 67, after the fifth, blue sparks played over it.
- **The explosion:** at 74 seconds an orange fireball, half again the
  ship's length, with pieces of it flying out; at 75 only the pieces,
  drifting; at 76 nothing. Each ship's `fireball` is `xfirebmd`
  (`craft.odf`, which they all build on): a `fireballexplode` of 2.5
  seconds whose model, `dbName` `xplode`, is `SOD\Xplode.sod`: eight
  sprite nodes, `critical1` to `critical8`, each a billboard of
  `explosion.spr` (`bigorange` 40 across, `sparkball1` and `sparkball2`
  smaller), each a 16-frame flip-book over 1.1, 1.3 or 1.5 seconds
  (`tex4x4_1.1`...) and shown until just before then (`explode1.1`:
  `draw`, 1 from 0, 0 from 1.099). Its nodes don't move. Its shockwave,
  `xshock`, follows 0.6 seconds on.

## In Reluva

- **Rules** (`Combat`): a unit's shields are its `maxHealth`, back at
  its `shieldRate` a second; its weapons are those of `weapon1` on
  without `special` (the tractor and mining beams), with their
  ordnance's damage, variance, speed and sprite. A hit is taken as the
  code above takes it (`Combat::hit`).
- **Fighting** (`MissionView`): the attack button, or A (`Input.map`'s
  `attack` shortcut, for one unit or a group), arms the attack cursor
  (`c_attack`); a click on a unit makes it the target of the selected
  units with weapons. A right click on another player's unit does the
  same *(guessed)*: the game's settings call its default the
  right-click interface, and no enemy was near enough to try. A right
  click on the map sends the ships there, giving their attack up.
- **Special weapons:** each unit's from its definition, its special
  energy as above, the rail's buttons dark but while one could be
  fired: researched if a pod gives it, charged, ready, and made. A
  button pressed fires it from each selected unit that can, or, for
  one that wants a target, at the next unit clicked. Made yet: the
  mines, each going for the nearest unit of another's, ship or station,
  in its sight and hitting it when within half its size *(guessed)*;
  the cannons and launchers whose ordnance does damage, as a shot; and
  the rest as the program's code runs them (their classes' update
  methods, followed in the disassembly), each its ordnance's
  `specialValue` in seconds where it has one *(from the code)* but where
  marked:
  - **toggles:** the cloaks, regeneration, the Corbomite Reflector and
    point defence come on for their `specialEnergyCost` and go off for
    nothing when pressed again. Regeneration, the reflector and point
    defence pay their cost again each second, going off when it can't be
    paid (0x501bf0, 0x501870, 0x501510); regeneration gives its
    `regenModifier` (30) of shields a second and every system whole, off
    once the shields are full. Under a cloak no special energy comes
    back, the unit doesn't fire, and it shows itself when hit (0x4f5d30,
    0x4f5e90); a phase cloak drains its `energyDrainRate` (15) a second
    besides, and shows the unit when it can't (0x4f6260);
  - **beams** (`toggleweapon`): on for their cost once, off pressed
    again; they reach by the gap between the two units, their centres'
    distance less both their sizes. The holding beam pays its cost a
    second, holds a unit of another's with its engines out, breaks past
    1.33 times its range, and sends `boardingSize` crew every
    `boardingInterval` after `boardingDelay`; the tractor beam takes only
    a ship, not a starbase, whose engines are out, derelict or slowed,
    and pulls it to its `targetRange` (60) from the holder (0x4e1c20);
    shield inversion takes `drainRate` (50) shields a second to the
    holder, at its cost a second, to 1.5 times its range; the energy
    converter gives the holder's special energy as shields one for one;
    the Borg bore takes `specialValue` (5) crew a second, at its cost a
    second, within its `attackRange`;
  - **area weapons** (`areamissile`, `areacannon`): one shot at each of up
    to `maxTargets` units in the program's order, by `hitCondition` (2,
    others'; 1, its own side's; 0, any), their gap against the range
    *squared*, as the program compares it (0x4ff55a), so near the whole
    map. The Energy Dissipator drains `specialEnergyDrainRate` (1000)
    special energy over `specialEnergyDelay` (10) seconds (0x4d9550);
    the Shield Disruptor takes the shields and their generator, the
    shields given back after unless the generator was knocked out
    meanwhile (0x4df810); the Myotron, the weapons (0x4d9fa0); Engine
    Overload, the engines; the Sensor Jammer, the sensors; the
    Holo-emitter turns a ship on its own side, its engines out; Death
    Chant makes its side's weapons reload `FireModifier` (1.5) times as
    fast;
  - **shots:** the Chain Reaction Pulsar, fired with no target, goes
    from the firer to the nearest unit within `chainRadius` (300), then
    on to the nearest not yet hit, up to `maxHits` (6), each hit
    `chainDamageModifier` (25) harder; the Polaron Torpedo does no
    damage but puts out one of the engines, weapons, life support or
    sensors, five tries for one the ship has; the repulsion field does
    its damage, puts out the engines and pushes a ship away at its
    `repulsionForce` (50), a starbase not at all; the Psychonic Blast
    kills its `percentage` (half) of the crew; the Klingon commandos take
    up to `partySize` (40) of the firer's crew as a party landing after
    `timeDelay` (5) seconds; the engineering team raises an ally's
    shields to `shieldsReturned` (half) of their most and makes its
    systems whole;
  - **the rest:** a Gravity Mine does no damage but slows the ship to its
    `slowPercent` (50) of its speed (0x4e9a70), for 20 seconds
    *(guessed)*; self-destruct, after its `timer` (5), its shockwave
    `xselfd`'s 380 give or take 10 within its `radius` (300), less with
    the distance, the falloff *(guessed)*; the black hole destroys its
    ship, its rift growing from `initialScale` to `maxScale` over
    `expansionTime`, holding, then shrinking, hurting every unit within
    its `Radius` times its size; the Temporal Stasis Field freezes the
    units not of the firer's side within 400 for its `lifeSpan` (45);
    Gemini Effect copies an allied ship for its `effectTimer`; Computer
    Override makes a ship the firer's player's; Nanites stop it taking
    orders; the Romulan Spy shows what's round it; the transwarp drive
    and conduit move a ship, or the ships near it, to a point seen. Not
    made: those the files mark unfinished ("THIS IS A BUG", "WRITE ME")
    and the Omega particle's tractor beam, a mission's.

  A pod built gives its player its weapon instead of a ship; one for a ship
  whose yard can't be built yet is dark, as the original's Corbomite
  Reflector, the Sovereign's of the Advanced Starship Yard, was with no
  shipyard *(inferred)*. Built, its model is fixed
  to the station's next pod node (`podHardpoints`), and goes dark in
  its station's menu. A mine gets up to its speed at its `shotAccel`.
- **Crew and systems,** as the game's code takes a hit (above); a
  derelict neither moves nor fires, regains nothing *(inferred)*, and
  doesn't keep its player in. Repair brings the shields back with the systems. A system
  with none left is out until
  repaired over its repair time: engines out, the ship stops; weapons,
  it doesn't fire; the shield generator, its shields don't come back;
  life support, it loses `lifeSupportLoss` percent of its crew a second
  *(inferred)*. The info
  panel shows the crew so, and each system's icon from green, whole, to
  red, out. A unit burns while one of its systems is out, as the
  original's caught fire after the first volley on its hull.
- **Boarding,** as the code below: the transport button, then a unit:
  the ship goes within 500 of it, then sends a party of 5 a second; to
  another's only while its shields are at 1% or less, landing a second
  later; to an ally's at once, while it's short of crew. It stops when a
  party would leave less than its race's `crewRetreatRatio` of its most
  aboard. On another's, the fight is `Combat::board`; a crew beaten,
  the unit is the boarders' player's, in their colour, counted lost and
  destroyed as a kill is; a derelict is the first party's.
- **Repair:** systems mend by themselves faster by the race's
  `repairStrength` and slower by `repairYellow` or `repairRed` with the
  crew below those shares *(inferred)*: the keys' names say so. Repair
  and Recrew, lit for a ship short of hull, systems or crew with a
  shipyard or starbase of its player's *(inferred)*, as the original's
  whole construction ship had it dark: the ship goes there, and is
  mended over half a minute by its `repairStrength` and recrewed from the
  pool, 20 a second *(guessed)*. A station's Recrew takes crew from the
  pool the same way, to its most.
- **Hit chance:** each shot hits by its weapon's `hitChance` with the
  modifiers for how it and its target move, a roll of Reluva's own;
  one that misses flies past. A unit is stopped below half a unit a
  second, at warp past its impulse speed, slow between *(guessed)*: how
  the game tells them apart isn't known.
- **Destroyed,** a unit breaks up: its model's faces, each a piece, at
  the chunk manager's density, as above, flying and turning by a roll
  of Reluva's own within the chunk manager's speeds, each about its
  own middle *(guessed)*; and its fireball's shockwave
  follows by its `shockwaveDelay`, a flat ring of `xshock`'s 16 rays
  in `wselfdes`'s texture and its colour, out at its rate to its
  `radiusFactor` times the unit's size, fading over its last
  `fadeFactor` *(guessed)*: what the factor multiplies isn't known, nor
  the fade. Only a unit as big as `shockwaveMinimum` (40) throws one:
  the original's construction ship, about 30, threw none, where
  Reluva's first drew a ring 300 across.
- **Chasing:** a ship whose target is out of its weapons' reach flies
  to the point on its way at 0.95 of that reach from the target, set
  afresh as the target moves, and stops there; within reach it holds
  where it is, as the original's destroyer did.
- **Shown:** the bar over a unit being attacked, and the info panel's
  shield bar, with their colours; the shots as their sprite, added on,
  along their way and turned to the camera.
- **The shields' glow:** at each hit the shields take, half an ellipsoid
  through the corners of the unit's box, its half towards the shooter
  *(guessed)*, with the ripple's 16 frames over 0.8 seconds across it
  (20 a second, as the eighth at 0.4), a new hit starting it over; in
  the shield bar's colour. A hit on the hull alone shows none
  *(guessed)*.
- **Burning:** a unit's damage emitters run once its hull is down to two
  thirds *(guessed)*: the original's ship burned after its sixth volley,
  a third of its hull gone, and not after its fifth.
- **The explosion:** its `fireball`'s model, at the unit's middle, from
  its death, for 1.5 seconds, its sprites' flip-books and `draw`
  animations played from then.

## Sources and checks

- **Where the facts come from:** the original's draws every second from
  45 to 140 seconds; its Defiant against the computer's starbase, its
  frames every two seconds and its memory at 243 seconds; the definitions (`fbase`, `fbpulse`, `fbpulseo`,
  `fconst`, the weapons' bases); the program's code, for a shot's
  damage, the system it hits and the debris.
- **How it was checked:** the unit tests (`storm3d.unit.play`) fire the
  starbase's volleys at the ship's vitals: three quarters of its shields
  after the first, none after the fifth, gone at the seventh, 27 seconds
  in, as the original's; and a pulse's chances by how it and its target
  move. `engine-shot` giving the same orders: the bar
  falling, and the ship gone, its officers back, as in the original's
  frames.

## Open questions

- What destroyed the original's construction ship at the starbase's
  seventh volley: by the code above it can't take a critical hit, its
  shares adding to 99.5, and its crew wasn't gone (213 just before);
  some other path, not found, or a part of the hit misread. And how it
  lost 110 crew while its shields lasted.
- Why all five system icons were red after one volley on the hull,
  and what sensors out do.
- The shield's shape and size exactly, and whether it glows when a hit
  gets past it.
- How fast and which way the debris's pieces fly and turn, within the
  chunk manager's limits,
  the shockwave's size and fade, and the blue sparks of hull hits.
- Where the game rolls a shot's hit, so how it tells a unit stopped,
  slow or at warp; what `damageThreshold` does.
- The special weapons not followed in the code: how long a Gravity
  Mine's slow lasts, the self-destruct shockwave's falloff, what a cloak
  detector's `detectClass` 1 and 2 do, how mines turn, and what the
  Ultritium ball does. None has been seen in the original: that wants
  its ships researched and in a fight.
- Which pod node the original takes first (its Antimatter Mines went
  to an arm, not known which), and the pod growing as it's built.
- How fast a yard repairs and recrews, and whether a starbase repairs
  too.
- When ships circle their centre of combat
  ([movement.md](movement.md#height-and-moving-in-a-fight)): neither the
  destroyer chasing a construction ship nor the Defiant against the
  computer's starbase did; ships against ships haven't been seen.
- The systems (engines, weapons, shields, life support, sensors) and
  their hit points.
