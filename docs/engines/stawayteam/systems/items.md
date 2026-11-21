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

Not there yet: mines, the scanners, the cloaking band, the other gadgets
and skills, the phaser's kill and stun settings, the cursors, the bar's
scrolling and the full inventory.

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
  officer and healing them.

## Open questions

- **Using items:** the program's rules for each Process: when the
  officer walks and when they use it, what the cursors show, and how a
  thrown item flies and bounces.
- **Grenades:** whom an explosion reaches and how far, whether it hurts
  the team, and what the EM Pulse Grenade does.
- **The bar:** where a count is shown, the More Indicator, and the full
  inventory.
- **The phaser's settings:** where the switch is (`Toggle` and the
  `CPhaserInfoRenderer`'s STUN and KILL) and how it's used.
