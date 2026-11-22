# Star Trek: Away Team: items

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/map_items.cpp`, `src/engines/stawayteam/ui/map_gadgets.cpp`, `src/engines/stawayteam/ui/game_hud.cpp`, `src/engines/stawayteam/game/team.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

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
Hand Grenade's projectile is a puff of particles.

A grenade's `Discharge Action` throws it from its `Height` (31) with a
`CThrowGravityAI` (`Gravity` 17, `Bounce Sound` `WEAPONS/Grenade
Bounce.ogg`); its model bounces by its `Bounce Factor` (0.6), `Bounce
Friction` (0.4) and `Min Bounce` (0.05).

The EM Pulse Grenade's Explosion Radial Damage is 0: its model's
Destroyed Action is a `CDoOnAllWithinRadiusAction` about it, 150 across
(`Y To X Ratio` 0.71), on `Who To Affect` `Electronic Equipment`: a noise
of 250 for 2 seconds as it starts, and each it reaches given a
`CAssignOverrideTemporaryTaskAction` of a `CWaitAI` of 15 seconds, with
sparks over it as long. In Mission 01 the Klingon prisoners are the
Electronic Equipment.

### The cloaking band

The Personal Visual Dampening Band ("Cloaking Band") is a
`CStarTrekInvisBandInventoryItem`, a charged item: its charge, out of
100, is spent at its `Energy Use` (4.5) a second while it's on, and comes
back at its `Energy Recharge` (0 for the band) while it's off; empty, it
goes off (`0x5d2c50`); it goes on only with an Energy Use's worth in it
(`0x5d2d30`). On, its wearer fades to a quarter seen, at a whole a
second, and back as it goes off (`0x5d3740`), with its `Use Sound`
(`Devices/Invis.ogg`). Its panel, a `CChargedItemInfoRenderer`, has its
indicator (`Foreground Indicator Sequence` over `Background Indicator
Sequence`, cut to the charge, `Clip Foreground As Charge Indicator`),
the charge at `Percent Pos`, and its button, `Button On Frame` or `Button
Off Frame` with DEACTIVATE or ACTIVATE. Its description says the wearer
can't be seen.

### Gadgets

The others' Process Activities say what they do, and what they put down
does the rest by its model's own AIs:

- **The audio decoy:** a `CDropItemAI`, within its `Item Range` (400):
  `Items/Decoys/Audio Decoy` put down with its sound; the decoy's
  `CActionAI`s make a noise of 200 for 2, send the enemies within 200 to
  look there, and delete it a second later.
- **The holographic projection:** a `CDropItemAI` (400): its model, of
  Category `Decoy,Player,Team Member`, with 35 `Hit Points`, its `Pain
  Sound` as it's hit, gone after 15 seconds; its Discharge Action's noise
  of 250.
- **The psionic projection:** a `CDropItemAI` (350): a figure that goes
  after 15 seconds, or is picked up (its process gives the item back).
- **The stasis field** (`Portable Containmnet Field`): a
  `CGotoPointAndDoActionAI`, walking to the place, then a `CDropItemAction`
  15 in front of the officer. Its `Deploy` sequence's last frame gives it a
  `CEveryTouchingOvalTriggerAI` (`X Radius` 30, `Triggered by` `Enemy,
  Player,Team Member`) holding whoever comes in without the field in their
  inventory to a `CDoNothingAI`, with `WEAPONS/Stasis Field.ogg`, and sets
  it to `activate`. Its own process, for one with the field, picks it up:
  it's given back, and those within 31 let go.
- **The teleportation brig:** a `CDropItemAI` putting down the brig and
  giving a `Telportation Brig Controller`. A body's death behaviour
  (`Death Behaviors/Klingon`, its `Default Death Type`) gives it a process
  for the controller: the officer walks to it and it's beamed away
  (`CAddEffectAction`, `Delete When Finished`, 1.75 seconds), and makes it
  `Dead Bodies` rather than `Enemy`.
- **Strikes:** the Vulcan Neck Pinch, Phase Neural Disrupter and Botanical
  Paralysis are `CStrikeTargetAI`s and `CStrikeTargetAction`s (`Strike
  Damage Radius` 40, 20), all with a `Weapon Damage` of 50,000; Botanical
  Healing heals as the hypospray, by 20.
- **Skills:** Computer Systems, Security Doors & Systems, Terminal Repair,
  Antimatter Charges, the Extractor and the Borg serum have no Process
  Activity of their own: the maps' parts have processes for them
  ([maps.md](maps.md#the-maps-triggers)).

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
kind shows. Its sequences' places are from its middle (`Phaser Indicator
Pos` 0, -8; `Indicator Position` -55, 0), its pictures' and words' from
the info bar's corner, which puts them on it (the phaser's `Stun Pos`
155, 39; the band's `Button Pos` 116, 13). A `CGenericInfoRenderer` its
`Text` in `Text Size` at `Text Pos`. The phaser's `CPhaserInfoRenderer`: its `Phaser Power Indicator
Sequence` at `Phaser Indicator Pos`, its damage and range at `Phaser
Damage Pos` and `Phaser Range Pos` in `Phaser Font`, and two buttons
(`Buttons/Stun Pos` 155, 39; `Overload Pos` 95, 39), each's words at its
`Text Pos`: STUN in Trek Small with it set to stun, else KILL in Trek
Small Red; OVERLOAD likewise. The phaser comes set to `Kill` (its
`Default Inventory Item`'s `Weapon Setting`); a map can set an enemy's
to `Stun` (34 Borg guns).

### The inventory bar

At Rules.txt's `Inventory Pos` (355, 419): its `Background`, and a
place for each of `Inventory/Item Types` (Gadget, Offensive Gadget,
Primary, Secondary, Skill, Special) from `Inventory Bar Offset` (37, 12),
`Inventory Icon Size` (34) and `Inventory Icon Spacing` (2) apart, each
showing one of the officer's items of its type in an `Item Frame`, the
one chosen on the `Selected Icon Background` (`0x41ae4b`), with the
`More Indicator` (`0x41ae65`); one with none darkened (`0x423fe2`).
Inventory Next and Prev go along a place's items and on to the next
place's (`0x423da0`, `0x423e90`; [maps.md](maps.md#the-interface)). Its
hint says "Right-click for full inventory" (`0x41b158`). The inventory
screens' `Skill Tab`, `Item Tab` and `Toggle` pictures aren't named by
the program.

### Its cursors

Each item names its cursors, of `Interface/Game Cursors`: its Process's
`Process Cursor` and `Can't Use Cursor`, its `Entity Hovering Cursor`,
and its `Obscured Cursor` if `Show Obscured Cursor`. The Tricorder's are
`Tricorder Active` and `Tricorder Inactive`, the Concussion Grenade's
`Concussion Grenade Active`, `Inactive` and `Obscured`, the Hypospray's
`Hypospray` over an officer and else `Hypospray Inactive`.

## In Reluva

Each officer's items are the roster's, with its Count, or for -1 its
definition's Rounds if it has any; the weapon they shoot with chosen
*(inferred)*. A click on an item on the bar, or its key (matched by the
keys' names, *inferred*), chooses it; a weapon chosen is the one they
shoot with. With a grenade chosen, a click on the map sends the officer
to throw it there: they walk until the place is in its Item Range and
in sight, then throw it from its Height, along the ground at its Max
Velocity and up so its Gravity, taken as pixels a second each 30th of a
second, brings it down there; it bounces by its Bounce Factor and
Friction, with its Bounce Sound, till a bounce is slower than 40 pixels a
second, and goes off its Shot Duration after it's thrown *(inferred)*.
A wall, or a collideable polygon standing full height while it's
active, turns it, mirrored off its side, with its Bounce Sound
*(inferred)*: Mission 01's and 08's `Fake Grenade Bouncer poly`, made
active for half a second by a pass-through trigger a grenade crosses
([maps.md](maps.md#the-maps-triggers)), is for that. Then its Explosion and Death Sound, its noise, and everyone in
its Explosion Radial Damage, in sight of it if it needs that, stunned for
its Stun Duration by one that does no damage, else hurt by its Weapon
Damage, the team too *(inferred)*. A grenade's own Destroyed Action,
more than a noise, runs where it went off: the EM pulse's holds the
Electronic Equipment within it 15 seconds, doing nothing *(inferred)*.
With a hypospray chosen, a click on an
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
typed out *(inferred)*. A part with its own process for the tricorder
(80 `Scan` processes in the maps, 48 `Medical Scan`) goes through that
instead, from within the tricorder's range, its line typed on the panel
([maps.md](maps.md#the-maps-triggers)).

The bar shows a place for each type, the last type's first as the team
selection has them *(inferred)*, each the item of its own the officer
last came to; Inventory Next and Prev, and the bar's buttons, go along
them, the item's name shown a second and a half; the More Indicator on
a place with more than one, where it's drawn *(inferred)*. The pointer
is the chosen item's cursors as it's over what it's used on, out of
sight, or elsewhere *(inferred)*.

The chosen item's panel is drawn on the info bar: its Background
Sequence centred at Info Renderer Pos (its frames' origin), its parts
placed as above *(inferred)*, over the minimap: a generic
one's text; the phaser's power, full, "Damage: %g" (`0x41bc47`), and its
STUN or KILL on the toolkit's toggle (`Inventory/Toggle Default` and
`Press`), a click on it switching the chosen officer's phaser
*(inferred)*, with OVERLOAD, off; the remote mine's DETONATE and its
count; the tricorder's words; the band's charge, as a percentage
*(inferred)*, and its button. Each officer's phaser is set as it comes,
and set to stun it stuns what it hits for its Stun Duration.

The cloaking band (`ui/map_gadgets.cpp`) charges and goes on and off as
above, from its panel's button, and its wearer fades; on, the enemies
don't see them *(inferred: from its description; the program's sight
isn't traced)*.

An item whose Process Activity puts something down, sets it up or runs an
action goes through it as a part's process does: a `CDropItemAI` walking
till the place is in its Item Range and sight, a
`CGotoPointAndDoActionAI` to the place, then their actions where it's
used, the officer the instigator *(inferred)*. What's put down becomes a
part of the map from its model's Default Entity: named, its sequence
played from its start once, then Idle, each frame's Trigger actions run
as it's reached, its triggers watched, its processes there to use, its
`CActionAI`s run at once *(inferred)*. The actions they need are there:
putting down (at the place, or in front of the officer), using up and
giving items, holding someone to a `CWaitAI` or `CDoNothingAI` and
letting them go (`CSendAIDoneMessage`), warping away, setting a part's
sequence and categories, checking for an item or a category, and noise.
An enemy that sees a Decoy goes after it and shoots it, its hit points
down by the weapon's damage and its Pain Sound, gone at none
*(inferred)*. A `CEveryTouchingOvalTriggerAI` answers to the enemies
its Triggered by names too *(inferred)*. Held, an officer or enemy does
nothing. A killed enemy's Death Behaviors' actions run, its categories
and Default Death Type its model's where the map leaves them out
*(inferred)*. A strike (`CStrikeTargetAction`) is made from within its
Strike Damage Radius, 30 at least, the officer walking up *(inferred)*,
and does its Weapon Damage.

Not there yet: the mines' Deploy and Activate sequences, the phaser's
overload and range, the Out Of Range cursor, the full inventory, the
Psychic Rack, the enhancements, the Psyche Displacement Controller, the
sparks and transporter pictures, the band on the team bar (`Active HUD
Sequence`), and walls in a grenade's way.

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
  phaser's panel with KILL and OVERLOAD as the info bar rises; Marcus
  Refelian's six places (grenade, phaser, tricorder with the More
  Indicator), Inventory Next from the tricorder to the Holographic
  Projection, the grenade and the phaser, its name under the bar; Ivan
  Verov's band on, from 100 to 0 in 22 seconds and off, the wearer faded
  to a quarter, its panel with 93% and DEACTIVATE; cloaked, Ivan Verov walking past the
  first Romulan unseen and putting the holographic projection down in
  front of it, the Romulan going after it and shooting it away;
  T'Andorla setting up a stasis field under a Romulan, which holds it
  once the field's deployed, then picking it up, the field hers again and
  the Romulan let go; a concussion grenade's arc from 31 up to 35 and
  down 200 on, two bounces and lying still; an EM pulse grenade thrown and
  its noise; the phaser's panel with its parts on it. Unit tests
  (`stawayteam.unit.maps`, kit) check the band's charge and fade and a
  guard not seeing her, a stasis field's oval holding a guard and the
  done message letting it go, a radius's action holding only the
  Electronic Equipment 15 seconds, a decoy put down in range and one used
  up, the guard seeing her with the band off, and a grenade's arc.

## Open questions

- **Using items:** the program's rules for each Process: when the
  officer walks and when they use it, what the cursors show, and how a
  thrown item flies and bounces (Gravity's units, how it turns off a
  wall).
- **The band:** whether the enemies' sight is what it beats, and how
  (its opacity, `0x496880`).
- **Gadgets:** how the enemies treat a decoy, whether the neck pinch and
  the paralysis stun rather than kill (their Weapon Damage is 50,000), and
  what the Psychic Rack and the enhancements do.
- **Grenades:** whom an explosion reaches and how far, whether it hurts
  the team, and what the EM Pulse Grenade does.
- **The bar:** where a count is shown, where the More Indicator is
  drawn, the order of its places, and the full inventory.
- **Cursors:** when the program shows each of an item's cursors.
- **The panels:** where each part of an item's panel is drawn, whether
  it's over the minimap, and how the phaser's switch is used.
- **Mines:** where one's put down, who a remote mine's DETONATE sets off,
  and the mines' sequences.
- **Tricorders:** what can be scanned, and what the Medical Tricorder
  shows that the Tricorder doesn't.
