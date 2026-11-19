# Midtown Madness 2: ambient sounds

> **Status:** done · **Code:** `src/engines/age/audio/ambient.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

The city's ambient sounds. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Ambient sounds

`AmbientSounds` (`ambient.cpp`) plays the city's ambiences
(`aud/ambient/<city>ambientcontainer.csv`; the layout in
[pathset.md](../formats/pathset.md)): San Francisco's birds over
the Presidio, the bells and sea lions along the piers, the gulls and
tug horns by the bay and the street cable under the cable car lines;
London's river and the Underground's station voices. Each is as loud
as the listener (the player's car) is near its nearest point: in full
within its least distance, falling evenly to nothing at its greatest
(the gulls 100-250 m, the cable 10-100 m). Its looped samples play while
it's heard; the others play now and then, a random time between the
sample's low and high limits apart, if it's heard then. That fall-off,
and treating sample types 1 and 2 alike, are Reluva's. Each sound is
panned to the side its nearest point is on (at most 0.8 of the way, and
less within its least distance, where it's all round). A sample with
**Doppler** set (the tug horns here; the ferry, the bridge and the
train too, which have no points and aren't played this way) is higher
as the listener comes nearer, by (343 + closing speed) / 343.
The **min speed** and **max speed** columns (0 and 999,999 for all)
are the listener's speeds it's heard between. At most 8 ambient sounds
play at once: the loops of the highest **3D priority** (12 for all but
the screams' 8) first, then the loudest; a one-shot only when there's
room. The **Audible area** is read by where the ambiences' points are:
1 (the tube's voices, 7-15 m below the streets) heard only from more
than 4 m underground, 2 (the buoys and sea lions, at the water) only
from above that, 0 anywhere.

## Sources and checks

- **Where the facts come from:** `aud/ambient/<city>ambientcontainer.csv` ([pathset.md](../formats/pathset.md)).
- **How it was checked:** `age.unit.city`; by ear.
