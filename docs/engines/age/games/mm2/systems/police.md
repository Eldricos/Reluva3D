# Midtown Madness 2: police

> **Status:** partial · **Code:** `src/engines/age/game/police.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The police: patrolling, chasing, busting, and their sirens. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Police

- **Starting** where the aimap puts them (their models too: `vpcop`, and
  in some Crash Course lessons sports cars, `vpauditt`, `vpdb7`,
  `vpmustang99`).
- **Patrolling** (on the Drive page): from the nearest lane going their
  way, along the traffic's lanes and on through the junctions, turning at
  random (not back along the same road where there's another way), a
  kilometre planned at a time, at the speed limit. They stop for a light
  that isn't green at the next lane end within braking distance, and
  keep 18 m behind anything ahead within 2.5 m of their way (the
  traffic, the player, the opponents, each other); held up like that the
  driver isn't asked, so it doesn't take the wait for being stuck. In
  races they wait where they're put instead (those places look chosen
  along the race's way). The aimaps give no patrol routes: their comment
  names a "Patrol Route" field, but the four numbers after a police car's
  heading (`0 15 0.5 50` in most lines; also `0 15 0 50`, `0 15 1 100`
  and `0 15 0 70`) don't say which car patrols or where.
- **Noticing** the player run into one or go past within 40 m (the
  aimap's `CopChaseDistance` where given) at more than half as fast
  again as the speed limit (22.5 m/s, about 50 mph).
- **Chasing**: along the road graph ([races.md](races.md)) to
  where the player is, routed again when the player has gone 20 m from
  it (or every 5 seconds), and within 30 m with nothing of the city in
  between, straight at a point a little ahead of the player, braking to
  box it in once it's slow; against something for 2 seconds, it backs
  out. Its light bar's two halves (`SIREN0`, `SIREN1`) and flares at its
  `SRN` squares, blue then red, alternate four times a second.
- **Busted**: held under 2 m/s within 9 m of a chasing car for 3
  seconds; `arrest` plays. On the Drive page the car is put back at the
  start; in a race it's over ("Busted").
- **Giving up**: left more than 300 m behind for 8 seconds, then back on
  patrol (on the Drive page) from where it is.

Measured over two minutes of San Francisco's and London's cruising
police on patrol, the player parked (scratch runs, not part of the
repository): every car patrolling, 830-900 m driven on average (the
slowest 221 m, held at lights and behind traffic), 11-12 put back after
being stuck, and 2 lights crossed on red in each city (turning red with
the car too close to stop). In chases (a car driven away from a cop at
30 m/s, then stopped), the cop caught up and busted it in three of four,
and lost it in the fourth.

### Sirens

The sirens follow the game's siren tables
(`aud/cardata/opponent/policesiren.csv`, and the player's
`aud/cardata/player/sfpolicesiren.csv` and `londonpolicesiren.csv`):
each names looped recordings, and for each, lines of "play time, next
index": how long to play it before going on to which. Reluva takes a
recording's lines in turn each time it comes round. The computer-driven
police in San Francisco use the opponents' table (`police1fastsirenloop`,
`police2hilowloop`, `police1whailerloop`); in London, which the
opponents' table has no recordings for, the player's London table
(`siren_london` and `siren_london2`, 15 seconds each). The nearest
chasing car's siren is heard, quieter with distance, out to 250 m.

Driving a police car on the Drive page (the menus' "Ford Mustang Cruiser",
`vpcop`), H turns its siren on and off: its light bar and flares, and the
player's table for the city.

### No radio

The game's audio has no police radio to play. `mm2audex.ar` and
`mm2aud.ar` hold engine and horn recordings, effects, the announcers'
speech (`aud11/as1`-`as5` for San Francisco, `al1`-`al6` for London: race
starts, results, each car, the weather and time of day) and the Crash
Course instructors' (`ccs`, `ccl`); the only police-related speech is the
announcers' for Cops and Robbers, a multiplayer mode
(`spchdata/CNRSF.csv`, `CNRLONDON.csv`: who has the gold, who dropped
it), and their lines about the police car itself
(`vpcop_results.csv`). The police sounds are the sirens and `arrest`.
`copchase.csv` (24 points in San Francisco, 7 in London, in the waypoint
files' form) is the way for a Crash Course lesson (San Francisco's
lesson 5, London's lesson 10, whose `crash<n>data.csv` names it), not a
patrol.

## Sources and checks

- **Where the facts come from:** the aimaps' police, the siren tables in `mm2aud.ar`; public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.city`; by hand.
