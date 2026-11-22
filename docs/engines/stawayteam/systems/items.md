# Star Trek: Away Team: items

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/map_items.cpp`, `src/engines/stawayteam/ui/game_hud.cpp`, `src/engines/stawayteam/game/team.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

Each officer carries items: weapons, grenades, mines, gadgets and
skills. On a map the chosen officer's are on the inventory bar, the one
they've chosen to use on its own background. What using an item does is
in its definition: how it's aimed, and what it does where it's used. A
grenade is thrown at a place and goes off there; a hypospray is used on
an officer and heals them.

## How it works

### What an officer carries

The roster gives each officer's `Inventory`, items by their `Inventory
Definition` with a `Count`, -1 for those not counted (the Tricorder, the
Phaser): Marcus Refelian carries a Tricorder, a Phaser, five Concussion
Grenades and a Holographic Projection. An item's definition
(`Inventory/Item Definitions`) has its `Default Inventory Item`, of a
class for its kind (`CStarTrekEnergyWeaponInventoryItem`,
`CStarTrekProjectileWeaponInventoryItem`, `CStarTrekCountInventoryItem`),
whose `Rounds` are its uses (the Hypospray's 5, a grenade's 3).

### Using one

An item's definition says what using it does:

- **Its `Process`**: `Process Type` (`Shoot` for 27 items, `Item use`
  14, `Hypospray Use` 4, `Scan`, `Medical Scan`, `Extractor Use` and
  others), the cursors shown (`Process Cursor`, `Entity Hovering Cursor`),
  and its `Process Activity`, the AI the officer runs to use it: the
  Hypospray's is a `CStrikeTargetAI`, which walks to the target, path
  finding on, and then uses it. `Can Use On Self`, `Item Range` (-1, or
  the grenades' 500) and `Need LOS` say where it can be used.
- **Its `Discharge Action`**: what it does then.
  `CShootEnergyWeaponAction`, `CShootProjectileWeaponAction` and
  `CStrikeTargetAction` shoot or strike
  ([maps.md](maps.md#fighting)); `CThrowProjectileWeaponAction` throws
  its `Projectile`, a model, with a `CThrowGravityAI` (`Gravity` 17, a
  `Bounce Sound`); `CGiveHealthWeaponAction` runs its `Health action`, a
  `CGiveHealthAction` giving its `Target` (`$Trigger`) `Health` (40 for
  the Hypospray). Its `Discharge Sequence` is the user's (`Shoot1`, a
  grenade's `Throw`), with the `Use Sound`.
- **Keys:** the keyboard's Inventory actions (Hypospray H, Tricorder,
  Medical Tricorder...) and Weapons actions (Phaser P, Concussion Grenade
  G, Neural Disruptor N...) choose the item of that kind; Inventory Next
  and Prev (`]`, `[`, the mouse wheel) go along them.

### Grenades

A grenade's model (`Player Shots/Concussion Grenade`) gives how it goes
off: its `Default Entity`'s `Shot Duration` (3 seconds), `Max
Velocity` (400), `Explosion` (`Explosions/Concussion Explode`), `Death
Sound`, `Explosion Radial Damage` (150; the Super's 200) and `Require
LOS For Damage` (1), and its Default Entity's `Destroyed Action`, a
`CAddNoiseAction` of 350 for 2 seconds. A concussion grenade does no
damage (`Weapon Damage` 0) and stuns for its `Stun Duration` (30). The
Hand Grenade's projectile is a puff of particles, and the EM Pulse
Grenade's Explosion Radial Damage is 0.

### Mines

A mine's Process Activity is a `CDropItemAI`: the officer walks to the
place, and its `CDropItemAtTargetAction` puts down `Model to Create`
("Items/Weapons/Deployed States/Proximity Mine"), with its `Deploy
Sequence`. The proximity mine's model's Default Entity has a
`CActionAI`: a `CDelayAction` of 2 seconds, then a `CAddAIAction` giving
it a `CTouchingOvalTriggerAI` (`X Radius` 100, `YX Ratio` 0.71,
`Triggered By Enemies`) whose Enter Action plays its `Activate`, its
pulse sound and a noise of 350 for 2, and destroys it 0.65 seconds on;
destroyed, it explodes, `Explosion Radial Damage` 150, `Require LOS For
Damage` 1, the mine's `Weapon Damage` 60. A remote mine
(`CStarTrekRemoteMineInventoryItem`, `Rounds` 5) is put down the same
way and has no trigger; its panel (`CDetonateInfoRenderer`, on
`Interface/Remote Mine Background`) has its `Detonate Button` (`Info
Bar/Remote Mine/Detonate Default` and `Press`, "Detonate" in Small Red)
and "Number of Mines".

### Tricorders

The Tricorder (`Process Type` `Scan`) and Medical Tricorder (`Medical
Scan`) are used on what's under the cursor (`Always Use Target Under
Cursor`) within their `Item Range` (450), their `Default Use Sound` the
tricorder's pulse; their `CTricorderModelDescriptionAction` shows the
model's `Description Script` and `Description Line` ("Model
Descriptions": "Romulan warrior of the Romulan Star Empire.") on their
panel (`CTricorderInfoRenderer`, on `Interface/Tricorder`), typed at
its `Text Speed` (30), in Rules.txt's `Tricorder Pos` and `Size` (192
by 36).

### An item's panel

The chosen item's `Info Renderer` is drawn on the info bar at Rules.txt's
`Info Renderer Pos` (127, 34): its `Background Sequence`, and what its
kind shows. A `CGenericInfoRenderer` its `Text` in `Text Size` at `Text
Pos`. The phaser's `CPhaserInfoRenderer`: its `Phaser Power Indicator
Sequence` at `Phaser Indicator Pos`, its damage and range at `Phaser
Damage Pos` and `Phaser Range Pos` in `Phaser Font`, and two buttons
(`Buttons/Stun Pos` 155, 39; `Overload Pos` 95, 39), each's words at its
`Text Pos`: STUN in Trek Small with it set to stun, else KILL in Trek
Small Red; OVERLOAD likewise. The phaser comes set to `Kill` (its
`Default Inventory Item`'s `Weapon Setting`); a map can set an enemy's
to `Stun` (34 Borg guns).

### The inventory bar

At Rules.txt's `Inventory Pos` (355, 419): its `Background`, the items
from `Inventory Bar Offset` (37, 12), `Inventory Icon Size` (34) and
`Inventory Icon Spacing` (2) apart, each in an `Item Frame`, the one
chosen on the `Selected Icon Background` (`0x41ae4b`), and a `More
Indicator` when there are more than fit. Its hint says "Right-click for
full inventory" (`0x41b158`).

## In Reluva

Each officer's items are the roster's, with its Count, or for -1 its
definition's Rounds if it has any; the weapon they shoot with chosen
*(inferred)*. A click on an item on the bar, or its key (matched by the
keys' names, *inferred*), chooses it; a weapon chosen is the one they
shoot with. With a grenade chosen, a click on the map sends the officer
to throw it there: they walk until the place is in its Item Range and
in sight, then throw it, and it flies straight there at its Max Velocity
and goes off its Shot Duration after *(inferred: the arc and bounces
aren't there)*; its Explosion and Death Sound, its noise, and everyone in
its Explosion Radial Damage, in sight of it if it needs that, stunned for
its Stun Duration by one that does no damage, else hurt by its Weapon
Damage, the team too *(inferred)*. With a hypospray chosen, a click on an
officer (themselves too, if it can) sends them there to heal them by its
Health, up to their whole *(inferred)*. Each use takes one of its count;
none left, it can't be used. The bar shows each item's uses left in its
corner *(inferred)*.

A mine chosen is put down where's clicked, the officer walking there
first *(inferred)*; drawn on the ground, its model's Default Sequence
over and over *(inferred)*. A proximity mine arms after its model's
delay, then goes when an enemy's in its oval: its Activate Sound and
noise, and its explosion its fuse later, as a grenade's, hurting by the
mine's Weapon Damage (read from its model's AI; *inferred* for running
it here). A remote mine waits for DETONATE on its panel, which sets off
every one put down *(inferred)*. A tricorder chosen, a click on a part
with a model (an enemy where it's got to) sends the officer within its
Item Range and sight, then shows the model's description on its panel,
typed out *(inferred)*.

The chosen item's panel is drawn on the info bar: its Background
Sequence centred at Info Renderer Pos (its frames' origin), its parts
at their places from there, over the minimap *(inferred)*: a generic
one's text; the phaser's power, full, "Damage: %g" (`0x41bc47`), and its
STUN or KILL on the toolkit's toggle (`Inventory/Toggle Default` and
`Press`), a click on it switching the chosen officer's phaser
*(inferred)*, with OVERLOAD, off; the remote mine's DETONATE and its
count; the tricorder's words. Each officer's phaser is set as it comes,
and set to stun it stuns what it hits for its Stun Duration.

Not there yet: the mines' Deploy and Activate sequences, the cloaking
band, the other gadgets and skills, the phaser's overload and range, the
cursors, the bar's scrolling and the full inventory.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; the roster, the item definitions and
  their models in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, Mission 01:
  the team's items and counts; Marcus Refelian's concussion grenade
  chosen and thrown at the nearest Romulan, walking into its range first,
  the grenade flying there, its explosion stunning the Romulan for 30
  seconds, four left; the bar with the grenade on its selected
  background. Not yet against the original running. Unit tests
  (`stawayteam.unit.maps`) check the counts, a concussion grenade
  stunning a guard in its reach and not one outside it, a grenade hurting
  an officer, none left after the last, and a hypospray walked to an
  officer and healing them; a phaser switched to stun stunning a guard
  unhurt, a remote mine put down and set off, a tricorder on a guard
  reading its description. Mission 01 run on: the panels read for the
  phaser (CPhaserInfoRenderer), the mines and tricorders; Marcus
  Refelian's phaser switched to stun stunning the first Romulan for 15
  seconds; T'Andorla putting a proximity mine down by it, armed two
  seconds on and going off, taking it from 100 to 40; Ivan Verov's
  tricorder on it: "Romulan warrior of the Romulan Star Empire."; the
  phaser's panel with KILL and OVERLOAD as the info bar rises.

## Open questions

- **Using items:** the program's rules for each Process: when the
  officer walks and when they use it, what the cursors show, and how a
  thrown item flies and bounces.
- **Grenades:** whom an explosion reaches and how far, whether it hurts
  the team, and what the EM Pulse Grenade does.
- **The bar:** where a count is shown, the More Indicator, and the full
  inventory.
- **The panels:** where each part of an item's panel is drawn, whether
  it's over the minimap, and how the phaser's switch is used.
- **Mines:** where one's put down, who a remote mine's DETONATE sets off,
  and the mines' sequences.
- **Tricorders:** what can be scanned, and what the Medical Tricorder
  shows that the Tricorder doesn't.
