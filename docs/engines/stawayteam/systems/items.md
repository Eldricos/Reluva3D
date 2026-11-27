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
  the grenades' 500) and `Need LOS` say where it can be used: within its
  range, measured on the ground with the map's foreshortening undone
  (up or down the map, × 1/0.7128, `0x5c6d78`), and in sight only with
  `Need LOS`, which no item sets (`0x5c6c4e`).
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
can't be seen, and that's done by the fade itself: the enemies' sight
(`0x434e90`, and the security cameras' `0x559f33`) goes through
`0x497e10`, which first asks the one looked at whether it shows more than
0.35 of itself (`0x4960d0`), and the band sets nothing else. So the
wearer is still seen for the first 0.65 seconds of fading, and seen
again as soon as they show more than 0.35 once it's off.

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
  Damage Radius` 40, 30, 20), all with a `Weapon Damage` of 50,000 and a
  `Stun Duration` of 30; the neck pinch's `Weapon Kills` is 0, the
  others' `Weapon Setting` Kill. Botanical Healing heals as the
  hypospray, by 20.
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
kind shows. Its sequences' places are added to a point the info bar
gives it (`0x534b52`; Reluva takes the panel's middle: `Phaser Indicator
Pos` 0, -8; `Indicator Position` -55, 0), its pictures' and words' are
on the info bar with nothing added (`0x534d61`), which puts them on it
(the phaser's `Stun Pos` 155, 39; the band's `Button Pos` 116, 13). A `CGenericInfoRenderer` its
`Text` in `Text Size` at `Text Pos`. The phaser's `CPhaserInfoRenderer`: its `Phaser Power Indicator
Sequence` at `Phaser Indicator Pos`, its damage and range at `Phaser
Damage Pos` and `Phaser Range Pos` in `Phaser Font` ("Damage: %3.0f",
or "Damage: ---" for an item without a Discharge Action; "Range:
%3.0f"; both as the item has them now, enhanced or not), overloaded the
seconds left, "OVERLOAD: %1.1f", at `Overload Offset` (`0x534c80`), and
two buttons (`Buttons/Stun Pos` 155, 39; `Overload Pos` 95, 39), each's
words at its `Text Pos`: STUN in Trek Small with it set to stun, else
KILL in Trek Small Red; OVERLOAD likewise, made only for an item that
`Is Overloadable` (`0x534ed1`). The phaser comes set to `Kill` (its
`Default Inventory Item`'s `Weapon Setting`); a map can set an enemy's
to `Stun` (34 Borg guns).

### Enhancement and overload

**Weapon Enhancement** (Equipment Enhancement), an engineer's skill, is
used on a team-mate (`Can Use On Self`): its Discharge Action, a
`CExecuteActionWeaponAction`, runs a `CEnhanceInventoryAction` whose
`Time To Enhance` (60; 60 if left out, `0x57ed8a`) is given to each of
their items that's a weapon, energy or counted (`0x57ede0`), as its
`Enhance Time`, whatever was left of it before (`0x5cf050`). It counts
down with the game's time (`0x5cdcf0`); while it's above 0 the item's
`Item Range` and `Weapon Damage` are a quarter more (×1.25, `0x5cded0`,
`0x5cfda2`). The team bar loads `Interface/Team Bar/Inventory Enhanced`
(`0x5e1ad0`), but nothing draws it (its place, `+0x158`, isn't read
again); nor is an item's `Active HUD Sequence` (the band's
`Invisibility Band`, the Psionic Projection's `Psyche Displacement`)
read by anything that draws.

A phaser that's `Is Overloadable` (only `Phaser Damage Increase
Overload`, the second of its upgrades) has its panel's OVERLOAD button:
pressed, it's set to overload for 10 seconds (`0x5350d7`), or back.
Overloaded, the item's Discharge Action, Sequence and Projectile are its
Overload ones (`0x5cf080`, `0x5cf3c0`): it's thrown, not fired. Counting
down it beeps, `Weapons/Overload.wav`, the next beep after the part of
its whole left, in seconds; run out, its `Overload Projectile` (a
concussion grenade's model) goes off where its holder is and the item is
gone (`0x5cf235`).

### The Psychic Rack

Slovaak's Psychic Rack (Enhanced Mind Meld) is a
`CStarTrekPsycheDisplacementInventoryItem`: used on an enemy within its
Item Range (300), he takes it over. It can't be used while it's on, nor
on one already taken, of the team, or of the category `Immune to Psyche
Displacement` (10 of the maps' enemies; `0x5d4ff0`); then its `Fail
Sound` plays (`0x5d5304`). It needs a use's worth of charge, its `Energy
Use` (3; `0x5d5145`). Taken over (`0x5d50e0`), the enemy gets a
`CPsycheControlAI` in place of its own mind, and the team's category for
the while (`0x5d522c`): the player chooses and orders it as one of the
team, the enemies go after it as they would an officer, and Slovaak
stands still. The charge goes down by its Energy Use a second while it
lasts (about 33 seconds from full) and comes back by its `Energy
Recharge` (0.03) a second.

Of the damage the enemy takes, Slovaak takes as much, but no more than
half the health he has left (`0x43b900`), so it can't kill him. The meld
ends as the charge runs out, as either of them dies (`0x43b8ce`), or with
the panel's BREAK CONTROL: the enemy's own mind back, Slovaak free, its
`Break Meld Sound` (`0x5d5420`).

Its panel (`CPsycheDisplacementInfoRenderer`) has its Meter Sequence,
the charge as a percentage at Percent Pos, and BREAK CONTROL and CENTER
on the `Info Bar/Psyche Displacement/Psyche` buttons (`0x54a42e`).

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
hint, its item's name and "(Right-click for full inventory)"
(`0x41b158`). The inventory screens' `Skill Tab`, `Item Tab` and `Toggle`
pictures aren't named by the program.

The bar's icons are a strip (`0x423560`) with a pop-up of its own, the
full inventory. A right-click on the bar opens it, or closes it; a left
click chooses the icon under the mouse, the pop-up's or the bar's, and
closes it (`0x423a60`). It closes too 1.5 seconds after the mouse leaves
the bar and it (`0x423940`, `0x423b8d`). Over each of the bar's places it
shows all the officer's items of its type, one above another from an
icon and a space over the bar, each on black and drawn as the bar's, the
chosen one on the Selected Icon Background (`0x424880`).

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
to throw it there: they walk until the place is in its Item Range, as
above (walking there, rather than refusing, *inferred*), then throw it from its
Height, as the program throws (`0x5f8fc0`): toward the place at 45° up,
at √(Gravity × 20.749 × distance) times its Discharge Action's `Velocity
Factor`, no faster than its model's Max Velocity *(inferred)*. Gravity is
its `Gravity AI`'s, in the particles' units, a second each second
([particles.md](particles.md#particles)). At a Velocity Factor of 1 that
would land it there; the game's 0.725 lands it about half way. Each time
it lands it bounces by its model's `Bounce Factor` of its fall and loses
its `Bounce Friction` of its speed along the ground; a bounce slower than
its `Min Bounce` is none, and less speed along the ground than its `Min
Bounce Friction` stops it (`0x51fc10`). So it skips on toward the place,
ending about there on a long throw and past it on a short one. It goes
off its Shot Duration after it's thrown.
Anything active and collideable in its way along the ground stops it,
whatever the height of either, seen or not: its move asks for what's
`Active` and `Collideable`, not `Visible`, `Half Height` or `Full
Height` (`0x4effe0`, `0x4ef720`), so a low box or a pit's polygon does,
as a wall does; but not a part with a pass-through trigger, which lets
it through ([maps.md](maps.md#the-maps-triggers)). A side turns it only
as it goes in: what's met takes away only what of its move goes into the
side (`0x4efcf1`), so one thrown from inside an outline, its thrower
standing in it, goes out. It's turned back, mirrored off the side it
meets, with its Bounce Sound *(inferred)*.
Mission 01's and 08's `Fake Grenade Bouncer poly`, made active for half
a second by a pass-through trigger a grenade crosses, is for that. A
thrown thing sets off the triggers `Triggered By Anything`, flying or
lying, but not those `Triggered By Projectiles`, which answer to the
energy weapons' shots.
It flies through people: either side of a collision can turn it down
(`0x4ef85b`, `0x4ef87d`), and a grenade's model has a
`CIgnoreCollisionAI` listing `CEntityWalking`, which turns down anything
of that class or one made from it (`0x4381c0`). Every character on the
maps is: a `CEntityWalking`, a `CEntityOverlayWalking` or a
`CEntityStarTrekTeamMember`, each made from the one before. For the same
reason they walk over one lying on the ground. Then its Explosion and Death Sound, its noise, and its
explosion (`0x496710`). That reaches every active character, the team's
too, within an oval of its Explosion Radial Damage across and that over
1.403 up and down (`0x4883bf`), in sight of it if it needs that
(`0x4883b8`). Each is hurt by less the farther off it is, from twice
its Explosion Radial Damage at its middle evenly to none at the edge
(`0x488357`): the explosion (`0x496710`) asks with its radius and,
doubled, the hurt (`0x49676b`), and each reached is hurt by that with
its model's `Collide Damage Type` (`Unavoidable` for the grenades,
`Physical` for the mines) and the thrower as its instigator
(`0x488330`, `0x495220`). The item's own Weapon Damage and Stun
Duration play no part, and nothing on the way stuns: a concussion
grenade's 150 hurts by 300 at its middle. A grenade's own Destroyed Action,
more than a noise, runs where it went off: the EM pulse's holds the
Electronic Equipment within it 15 seconds, doing nothing *(inferred)*.
With a hypospray chosen, a click on an
officer (themselves too, if it can) sends them there to heal them by its
Health (`CGiveHealthAction`, `0x4d6cd9`), divided by their Stamina as
their hurts are, up to their whole
([maps.md](maps.md#fighting)). Each use takes one of its count;
none left, it can't be used. The bar shows each item's uses left in its
corner *(inferred)*.

A mine chosen is put down where's clicked, the officer walking there
first (its `CDropItemAI`'s `Move Behavior`, `Do Path Finding`); drawn on the ground, its model's Default Sequence
over and over *(inferred)*. A proximity mine arms after its model's
delay, then goes when an enemy's in its oval: its Activate Sound and
noise, and its explosion its fuse later, as a grenade's, hurting from
twice its model's Explosion Radial Damage (150 for each of the mines)
as above. A remote mine waits for DETONATE on its panel, which sends a message
on the player's `Remote Mine` broadcaster (`0x48bca0`), setting off
every one they've put down; its Destroyed Action makes a noise of 350
and shocks the living within 150 (an `Electric Shock` effect). A tricorder chosen, a click on a part
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
one's text; the phaser's power, full, its damage and range, its overload's
seconds left, and its STUN or KILL on the toolkit's toggle (`Inventory/Toggle Default` and
`Press`), a click on it switching the chosen officer's phaser
*(inferred)*, and OVERLOAD likewise, for a phaser that has it; the remote mine's DETONATE and its
count; the tricorder's words; the band's charge, as a percentage
*(inferred)*, and its button. Each officer's phaser is set as it comes,
and set to stun it stuns what it hits for its Stun Duration.

The cloaking band (`ui/map_gadgets.cpp`) charges and goes on and off as
above, from its panel's button, and its wearer fades, unseen by the
enemies while they show 0.35 or less of themselves.

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
A hologram is found as an officer is, the nearest of an enemy's Valid
Targets that it sees (`0x5dd690`; it's `Player,Team Member` too): one
nearer than any officer seen is gone after. Shot, its hit points go
down by the weapon's damage, with its Pain Sound, and it's gone at none
*(inferred)*. A `CEveryTouchingOvalTriggerAI` answers to the enemies
its Triggered by names too *(inferred)*. Held, an officer or enemy does
nothing. A killed enemy's Death Behaviors' actions run, its categories
and Default Death Type its model's where the map leaves them out
*(inferred)*. A strike (`CStrikeTargetAction`) is made from within its
Strike Damage Radius, 30 at least, the officer walking up *(inferred)*,
and, as a shot does (`0x5cf4d0`), does its Weapon Damage of its Damage
Type if it's set to kill, else stuns for its Stun Duration. The Phase
Neural Disrupter and the paralysis are set to Kill, so they kill; the
neck pinch, a `CStarTrekHandWeaponInventoryItem`, has `Weapon Kills=0`
where the others have a Weapon Setting, so it stuns.

The Psychic Rack is as above. Out of range, Slovaak walks into it first
*(inferred)*. The enemy taken over is chosen in his place, chosen again
with a click on it, and sent with a click: after an enemy clicked,
shooting it in its weapon's range and sight, else to the place; let go,
it's on patrol *(inferred)*. Its panel shows the meter's frame for the
charge left, BREAK CONTROL and CENTER while it's on, else its Text
*(inferred)*; CENTER brings the view to the enemy.

An energy weapon (`CStarTrekEnergyWeaponInventoryItem`: the phasers,
the disruptors, the Borg guns) has an energy of 100 (`Current Energy`,
the item's `+0x2c`): a shot needs its `Energy Use` (20 for most) as well
as its Reuse Delay past (`0x5d0980`) and spends it (`0x5d094b`); it
comes back at its `Energy Recharge` a second (2.5 for most), to 100
(`0x5d08a8`). So five quick shots, then one each eight seconds. The
enemies' are theirs alike.

An enhancement, kept for each officer rather than each item (it's given
to all their weapons at once), multiplies their weapons' damage and
range by 1.25, shown in the phaser's panel. An overloaded phaser can't be fired; thrown, it goes off as its
overload runs out *(inferred)*; going off, its projectile explodes as a
grenade does, its holder too ([grenades](#grenades)).

While the enemy taken over is chosen, its bar holds the Psychic Rack's
`Controller Item`, the Psyche Displacement Controller (given it as it's
taken, `0x5d5233`), and its panel, a `CPsycheControllerInfoRenderer`:
the charge and BREAK CONTROL.

A mine put down plays its drop action's `Deploy Sequence` once, then its
model's Default Sequence; set off, its `Activate`, its last frame held
*(inferred for all but the Activate the proximity mine's Enter Action
plays)*. A part beamed away (a `CAddEffectAction` that's `Delete When
Finished`) has its `Warp Overlay Effect` drawn over it: the overlay's
frames are `Num Rotations` (4) directions' worth, the figure's
direction's chosen when its own sequence has as many, else the first,
played over the warp, raised by its `Overlay YOffset` (`0x584100`,
`0x563940`).

The warp's behaviour (`CStarTrekWarpEffectBehavior`, `0x583a00`; its
`Fade In` at `+0x8`, `Warp In` at `+0x9`, read at `0x58358c` and
`0x58350c`) counts its time up to its `Warp Time`. With Fade In it holds
for the first quarter (0.25 at `0x617400`), and over the rest a part p
goes from 0 to 1. The `Warp Effect`'s amount is 1 − p; with Fade In the
figure's shown amount (its `+0xd0`, the cloak's too) is 1 − p, or p
with Warp In, and 0, or 1, once it's done. Every warp in the maps has
Warp In 0, the team's beaming in as well as the enemies' beaming out.

The `Warp Effect` (`CSimpleWarpEffect`, `0x563f80`) names a sequence of
21 frames of 64 by 64 noise (`Transporter Effect`, `Transporter Klingon
Effect`, `Transporter Romulan Effect`, `Borg Trans Effect`), each with a
frame rate of 0. So the frame is the amount's share of the 20 after the
first, rounded (`0x564bbf`). Each pixel the figure shows is moved the
amount of the way to the noise's pixel, which is laid from the figure's
origin over and over, in each 16-bit channel (`0x564d45`). At 1 the
figure is all noise in its own shape (`0x564ee0`).

Reluva draws the noise so, on the parts beamed away and on the team
beaming in. The parts are shown as the behaviour says. The team fades in
over the warp *(inferred: by the behaviour they'd end unseen, so
something else, not found, shows them)*. The blend is even in the
amount *(inferred: the program's steps come from a table it makes as it
runs)*.

The EM pulse's sparks over what it holds are its entity's particle
system ([particles.md](particles.md)).

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; the roster, the item definitions and
  their models in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, Mission 01:
  the team's items and counts; Marcus Refelian's concussion grenade
  chosen and thrown at the nearest Romulan, walking into its range first,
  the grenade flying there and going off by the Romulan, four left; the bar with the grenade on its selected
  background. Not yet against the original running. Unit tests
  (`stawayteam.unit.maps`) check the counts, a concussion grenade's
  blast killing a guard in its reach and not one outside it, a grenade hurting
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
  guard not seeing her as she walks in front of it, a stasis field's oval
  holding a guard and the done message letting it go, a radius's action
  holding only the Electronic Equipment 15 seconds, a decoy put down in
  range and one used up, the guard not seeing her till she shows more
  than 0.35 with the band off, and then going for a hologram nearer than
  her, and a grenade's arc.

## Open questions

- **Using items:** the program's rules for each Process: when the
  officer walks and when they use it, what the cursors show, how a
  thrown item turns off what it meets, and whether its speed is held to
  its model's Max Velocity.
- **Gadgets:** how an enemy shoots a hologram, and how the player orders an enemy taken over, and how its own mind comes
  back.
- **Overload:** whether a thrown one keeps its countdown (the item's
  `+0x18`, `0x5cf3e0`) or takes its projectile's own Shot Duration.
- **Grenades:** what the EM Pulse Grenade does beyond holding the
  Electronic Equipment, and how a damage type's `Unavoidable damage`
  weighs against a character's own (`0x495297`).
- **The bar:** where a count is shown, where the More Indicator is
  drawn, the order of its places, and whether the full inventory's
  items are in their Item Frames.
- **Cursors:** when the program shows each of an item's cursors.
- **The panels:** where each part of an item's panel is drawn, whether
  it's over the minimap, how the phaser's switch is used, and how its
  Power Indicator shows its energy, in a shot's worth of it a step
  (`0x534890`); Reluva draws it full.
- **Mines:** where one's put down, who a remote mine's DETONATE sets off,
  and the mines' sequences.
- **Tricorders:** what can be scanned, and what the Medical Tricorder
  shows that the Tricorder doesn't.
