# Star Trek: Armada: combat

> **Status:** partial · **Code:** `src/engines/storm3d/play/combat.cpp`, `src/engines/storm3d/play/mission_view.cpp` · **Checked against:** the original running a skirmish in Reluva's x86 emulator, its draws every second as its starbase fought one of its own construction ships · **Confidence:** seen, the hull's share inferred from one kill

## Overview

A unit with weapons attacks what it's told to: its attack button, then
a click on the target. Each weapon fires on its own `shotDelay` while
the target is in its `range`; each shot flies at its ordnance's
`shotSpeed` and does its `damageBase`, give or take its
`damageVariance`. Damage comes off the shields first, which come back
at the unit's `shieldRate`; past them, off the hull, until the unit is
destroyed.

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
  the ship lasted the sixth and not the seventh, 27 seconds in. Of what
  got past its shields, 277 by the sixth and 437 by the seventh, with
  its hull of 700 (its `maxHealth` again, *(inferred)*), the hull takes
  between 1.6 and 2.5 times a shot's damage.
- **Destroyed,** it left debris and its officers went back to the count
  (4 to 2 of 120); the selection lost it.

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

- **Rules** (`Combat`): a unit's shields and hull are both its
  `maxHealth`, its shields back at its `shieldRate` a second; its weapons
  are those of `weapon1` on without `special` (the tractor and mining
  beams), with their ordnance's damage, variance, speed and sprite. A
  hit takes the shields first, and what gets past them twice over off
  the hull, within the original's 1.6 to 2.5 *(guessed)*.
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
  Antimatter Mines, each going for the nearest ship of another's in its
  sight and hitting it when within half its size *(guessed)*; and the
  cannons and launchers whose ordnance does damage, as a shot. A pod
  built gives its player its weapon instead of a ship, and goes dark in
  its station's menu.
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
  45 to 140 seconds; the definitions (`fbase`, `fbpulse`, `fbpulseo`,
  `fconst`).
- **How it was checked:** the unit tests (`storm3d.unit.play`) fire the
  starbase's volleys at the ship's vitals: three quarters of its shields
  after the first, none after the fifth, gone at the seventh, 27 seconds
  in, as the original's. `engine-shot` giving the same orders: the bar
  falling, and the ship gone, its officers back, as in the original's
  frames.

## Open questions

- The hull's share exactly, and what `damageThreshold`,
  `shieldCrewModifier` and `hullCrewModifier` do (crew lost to hits).
- The shield's shape and size exactly, and whether it glows when a hit
  gets past it.
- The debris, the shockwave (`xshock`), the blue sparks of hull hits,
  and when the fires start: by the systems' damage, likely.
- The hit chance's part (`hitChance`, 1 for these).
- The other special weapons: the cloaks, the beams, the wormholes, the
  area weapons and the rest, each its own; and how many mines a ship
  drops, how they move (`omegaTurn`, `shotAccel`) and what they do to
  shields.
- The pod's model on its station (`podHardpoints`), not drawn yet; and
  why Corbomite Reflector was dark before any research.
- What `RTS_CFG.h`'s `DOGFIGHT_RANGE` (70) and anchor settings do: the
  destroyer chasing a construction ship didn't circle it. And what a
  right click on an enemy does.
- The systems (engines, weapons, shields, life support, sensors) and
  their hit points.
