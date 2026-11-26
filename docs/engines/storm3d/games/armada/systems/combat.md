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

## In Reluva

- **Rules** (`Combat`): a unit's shields and hull are both its
  `maxHealth`, its shields back at its `shieldRate` a second; its weapons
  are those of `weapon1` on without `special` (the tractor and mining
  beams), with their ordnance's damage, variance, speed and sprite. A
  hit takes the shields first, and what gets past them twice over off
  the hull, within the original's 1.6 to 2.5 *(guessed)*.
- **Fighting** (`MissionView`): the attack button arms the attack cursor
  (`c_attack`); a click on a unit makes it the target of the selected
  units with weapons. Each weapon fires when it's ready and in range,
  its damage rolled by a generator of Reluva's own; the shot flies to
  the target's middle and hits there. A unit whose hull goes is hidden,
  out of the selection and the minimap, its officers back to the count.
- **Shown:** the bar over a unit being attacked, and the info panel's
  shield bar, with their colours; the shots as their sprite, added on,
  along their way and turned to the camera.

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
- The shields' glow where shots hit (a mesh of its own), explosions and
  debris, and the hit chance's part (`hitChance`, 1 for these).
- Ships that chase what they attack, and right-clicking an enemy.
- The systems (engines, weapons, shields, life support, sensors) and
  their hit points.
