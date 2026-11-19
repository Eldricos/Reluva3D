# Midtown Madness 2: damage

> **Status:** done · **Code:** `src/engines/age/game/damage.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

How a car is damaged: its damage figure, its smoke, its damaged textures, and wrecking. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Damage

`tune/vehicle/<car>.vehCarDamage`:

- **MaxDamage** (the Beetle's 321,300), **MedDamage** (150,000),
  **ImpactThreshold** (1,500), **RegenerateRate** (0 for all);
- **TextelDamageRadius** (2.7): metres round a knock that look damaged;
- **SmokeOffset**, **SmokeOffset2**: where the smoke comes from, in the
  car;
- the smoke's birth rule, as `tune/effects/*.asBirthRule`: **Position**,
  **Velocity**, **Life**, **Mass**, **Radius**, **Drag**, **DRadius**,
  **DAlpha**, **Gravity**, **TexFrameStart**/**End**, **Color** (a
  signed ARGB: -167772161 is white, 0xF5 opaque) and their **Var**
  ranges.

Reluva reads the knocks' impulse (kg m/s) as damage: a knock over
ImpactThreshold adds what it's over by, up to MaxDamage; driving the
Beetle into walls for two minutes, its hardest knock was about 10,000. From MedDamage the car
smokes, from 10 puffs a second to 30 near wrecked, from each offset: a
billboard of a frame of `texture/fxpt8` (four smoke puffs, two by two;
frame 2 is the dark one), rising (Gravity is up: 15, where the falling
effects' are -9.8), growing by DRadius a frame (at 30 a second) and
fading over its life (and by DAlpha, in 255ths a frame), slowed by its
Drag. Which texture the frames are of is a guess. The same rules make
the props' and the wheels' particles (see
[traffic.md](traffic.md) and the materials above). At
MaxDamage the car is wrecked: its engine stops; the Drive page puts it
back mended after a moment, a race ends ("Wrecked"). Each knock's place
takes the body's parts within TextelDamageRadius to their damaged
textures (`<texture>_dmg`: the Beetle has them for its front, back and
sides), whole parts at a time rather than the game's painting round the
point. The damage needle, and a damage meter on the HUD, show it.

## Sources and checks

- **Where the facts come from:** `tune/vehicle/<car>.vehCarDamage`, the `_dmg` textures; the damage meter as public gameplay videos ([research/footage.md](../research/footage.md)) show it.
- **How it was checked:** `age.unit.driving`; by hand on the Drive page.
