# Midtown Madness 2: music

> **Status:** done · **Code:** `src/engines/age/audio/music.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The game's DirectMusic tunes, following the race or the drive. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Music

The game's DirectMusic music ([directmusic.md](../../../../../common/audio/directmusic.md)),
played by `music.cpp` on Reluva's DirectMusic player and DLS synthesizer.
A race picks one of the five race tunes of
`aud/dmusic/csv_files/singlerace.csv` (Enemy, Bullet, Duck, London,
PimpHand), the Drive page one of the four cruising tunes of
`singleroam.csv` (SunRoof, TacoLoco, LeTigre, BackOff); which one is
Reluva's choice, by the race (or city and car). Its segments follow the
play:

| State | Segment |
|---|---|
| started | Start |
| stopped (under 1.5 m/s) for 4 s | Idle, or Idle Cops if chased |
| chased by the police, moving | Cops |
| a circuit's final lap, or only the finish left in a checkpoint race or Blitz | Cops |
| moving again (over 5 m/s) after stopping or a chase | Return |
| the race over | Results |

Each change waits for the next measure, as DirectMusic's do. The speeds
and times for idling are Reluva's. The footage
([footage.md](../research/footage.md)) shows the rest. The beatless start
of the Start segment runs about 8-15 s. A few seconds after "Final lap!"
the tune speeds up, and the same happens after a checkpoint race's last
checkpoint. That faster groove matches the tempo of the tune's chase
segment, read from the `.sgt` files' tempo tracks: Bullet races at 140
bpm and its Cops segment is at 155, and the 0.78 s pattern heard after
Bridge Bash's last checkpoint is two of its beats. The beat stops in the
seconds after the finish, then Results plays.
Restarting a race doesn't restart the tune: in the footage the groove
runs straight on into the new attempt, and Reluva's does too. Paused,
the tune's Pause segment plays (no footage has the pause menu). Both lists' last three columns
name the **big-air motif**: GrooverStyle's `BigAir` pattern (a motif,
one measure of two parts) with the `BigAir` band. A second off the
ground (Reluva's measure of big air), it plays once over the tune, from
then, its band's instruments on its own channels.

## Sources and checks

- **Where the facts come from:** `aud/dmusic` and its `csv_files`; the music heard in public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `common.unit.directmusic`; by ear.
