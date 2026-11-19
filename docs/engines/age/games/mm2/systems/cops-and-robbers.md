# Midtown Madness 2: Cops & Robbers

> **Status:** done · **Code:** `src/engines/age/game/cops_robbers.cpp` · **Checked against:** the code only · **Confidence:** inferred unless marked

## Overview

Cops & Robbers: the gold, the bases, and the rules Reluva plays it by. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Cops & Robbers

The event's description (`jpg/race_cop`) reads: "Get the gold to the
bank or your hideout – without getting robbed by your opponents!"

**What it's played between.** `race/<city>/multicopwaypoints.csv` (44
places in San Francisco, 46 in London) is in the waypoint files' form:
x, y, z and a heading, the other columns 0. The places are spread across
each city at the roadside, and each heading runs along its road. From
them, by the session's seed, Reluva picks:

- The gold's home, at random.
- A base 300 m or more from it.
- In the team games, a second base 300 m or more from both, and about as
  far from the gold as the first, so neither side starts nearer. Which
  places the original uses for what isn't in the file.

**The games** (the Host Race Menu's GAME TYPES, with `mmlang.dll`'s
names):

| Choice | Picture | Sides |
|---|---|---|
| Free-For-All | `free_cr` | each player on their own; one hideout for everyone |
| Cops & Robbers ("Cops vs. Robbers") | `cpsvr_cr` | the robbers (red) deliver to their hideout, the cops (blue) to the bank. The cops drive the police car (`vpcop`), with its siren |
| Robbers Teams | `robrs_cr` | red and blue, each to its own hideout |

**The rules**:

- **Picking up.** The gold (`geometry/wpobj_gold`) lies turning at its
  home. A car within 5 m of it, and 4 m above or below, picks it up; the
  nearest wins.
- **Carrying.** The gold rides above the car that has it, and adds the
  host's GOLD MASS to that car's weight. The choices are the game's three
  (`mmlang.dll` 506-508): Weightless, Quarter Ton and Half Ton, taken as a
  US ton's (0, 227 and 454 kg). Half Ton is the default.
- **Stealing.** A car of another side that rams the holder knocks the
  gold loose where it is:
  - "Rams" means their middles come within 4.5 m, closing at 4 m/s or
    more.
  - A new holder keeps it for a moment and a half whatever hits it.
  - The car that dropped it can't take it back for 3 seconds.
- **Delivering.** Within 12 m of your side's base, the gold is delivered:
  a point for the side, and the gold is back at its home.
- **Returning home.** Left lying for a minute, or fallen out of the city,
  the gold goes back home.
- **Limits** (GAME LIMITS):
  - NONE: the host ends the game.
  - TIME: the game's 5, 10, 20 or 30 minutes (`mmlang.dll` 510-513),
    with its "N minutes remaining" at 20, 15, 10, 5 and 1.
  - POINT: the first side to that many deliveries (1-50) wins. The game's
    own point limits are 100, 250, 500 and 1,000 points (514-517). What a
    delivery is worth in them isn't in the files, so Reluva counts
    deliveries.

The host runs the gold. The others hear where it is, who has it, the
scores and the last few events, and say them with the game's words
(`mmlang.dll`):

- "You have the Gold!" / "*name* has the Gold!"
- "You dropped the gold!" / "*name* dropped the Gold!"
- "Gold delivered!" / "*name* delivered the Gold!"
- "Time's up!" and "Point limit reached".

**The announcer.** Each event plays one of the announcer's lines, from
`aud/spchdata/CNRSF.csv` and `CNRLONDON.csv`:

- The robber teams' lines name the team: BLUETEAMHASGOLD,
  REDTEAMDROPPEDGOLD, ...
- Cops & Robbers' lines name the side: ROBGETLOOT, COPRECOVERLOOT,
  COPSTASHLOOT, ...
- A free-for-all uses the robbers' lines.

Each table names a header row, `<EVENT> header`, then a recording's
prefix (`AS1\AS1ROBROB`), the last number and the number before the
first. London's tables add how many are used. So the lines are
`aud/aud11/as1/AS1robrob10.11k.wav` and so on. All 34 recordings the two
tables name are there.

**On screen.** The HUD shows the score: each side's, or the best four
players'. The arrow above the car points to the gold, or to your base
when you have it. The map shows the gold and the bases, yours in the
finish's colour. The checkpoint stands mark each base.

## Sources and checks

- **Where the facts come from:** `multicopwaypoints.csv`, the gold's model, the event's description and the announcer's lines.
- **How it was checked:** `age.unit.network`.
