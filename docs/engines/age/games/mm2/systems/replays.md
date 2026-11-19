# Midtown Madness 2: replays

> **Status:** done · **Code:** `src/engines/age/game/race_replay.cpp`, `src/engines/age/formats/replay.cpp` · **Checked against:** the code only · **Confidence:** Reluva's own unless marked

## Overview

The instant replay, and replays kept, saved and played again. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

The game's instant replay, and replays kept: Reluva's own files
(`src/engines/age/formats/replay.h`).

**What a replay is.** It's the race run again, not a film of it. Every
race (and lesson) is recorded as it's driven:

- what it was started with: the city, the race, the level, the car and
  its colour, and the options that change how the car drives or what's
  in the city (auto reverse, steering sensitivity, pedestrians shown);
- each frame's time step and the keys held.

Played back, the race is started as it was and stepped with those same
times and keys. Everything else follows from the race's seeds and the
player's driving: the opponents, the traffic, the pedestrians, the
police, the props knocked down. So it all comes back as it was, for a
few bytes a frame (a minute and a half is about 9 KB).

**Checking it.** Every 60 frames the recording keeps where the player's
car was. A replay whose car isn't within 5 cm of the place says it "has
gone its own way". That would happen if Reluva's driving or city changed
since the replay was saved. A Blitz driven, replayed at once, saved, and
loaded again from the main menu ran its 105 seconds to the same win at
the same time, with no difference at any of its marks. For that,
everything random or timed in the city starts afresh with each race:
the traffic's, pedestrians' and police's random choices from the race's
seeds, and the traffic lights' clock from 0. (Without the police's and
the lights', a cruise's replay drifts from 13 seconds in, as comparing
two runs frame by frame shows.)

**Watching.** After a race, **I** starts the instant replay of it. The
main menu's INSTANT REPLAY (`main_rpl`) opens the game's **Replay**
dialog (`jpg/rpl_bk`, "Choose a replay file"), which lists the replays
saved, with LOAD (`RPL_LOAD`), EDIT (`rpl_edit`: renamed in the game's
"Rename Replay" dialog, `jpg/REDIT_BK`), DELETE (`RPL_DEL`) and CANCEL.

**The replay's bar** has the game's REPLAY MENU words (`mmlang.dll`
483-490):

| Button | Key | What it does |
|---|---|---|
| Play / Stop | Space | runs or stops it |
| Step | `.` | one frame on |
| Rew | Backspace | back five seconds (the race run again, unseen, from its start to there) |
| Cam | the Change Camera key | through the car's own cameras, and a roadside camera |
| Save | | the replay saved, under a name ("Save as") |
| Quit | Esc | for the race just driven, first the game's "Do you want to save this replay?" |

Left and Right move along the bar, and Enter presses.

The roadside camera stands 4 m up, 45 m ahead of the car and 10 m to one
side (the other side each time), and watches it pass. Once the car is
70 m away it moves on ahead.

Multiplayer games aren't recorded: the other players' cars come over
the network, not from the race's seeds.

## In Reluva

`RaceScreen` records and plays them back
(`src/engines/age/game/race_replay.cpp`), and `Replay`
(`src/engines/age/formats/replay.{h,cpp}`) is the file. They're Reluva's
own files, not the original's
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

**Where they're kept.** Saved replays go in the save folder's `replays/`,
as `<name>.rpl`:

- "RLVRPLY", a version byte (2);
- the start, as above, and (since version 2) the conditions it was run
  in when they were changed on the race menu (laps, opponents, time,
  weather, police, traffic, pedestrians: [drivers-and-records.md](drivers-and-records.md#changing-a-race));
- the frames (each a float time step, the keys, a gear change, a flag);
- the marks.

## Sources and checks

- **Where the facts come from:** the game's replay menus' pictures and words; the file is Reluva's own.
- **How it was checked:** `age.unit.game` (a race replayed the same); by hand.
