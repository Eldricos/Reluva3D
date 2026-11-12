# Clue: Murder at Boddy Mansion: hot spots, weather and sounds

> **Status:** done · **Code:** `src/engines/clue/play.cpp` · **Checked against:** `Clue.exe` 2.2's code, and a recording of the original · **Confidence:** confirmed unless marked

## Overview

The rooms' hot spots, which react when clicked; the weather (the drapes,
the wind and the thunder); and the game's sounds: how fast they play, over
the menus, footsteps, and the fire and the fountain.

## How it works

**Hot spots.** Furniture whose object has states 20, 21 or 22 (`..._ag`,
`..._ab`, `..._an` animations: good, bad, neutral) reacts when clicked,
with one of them by chance: the bear dances or mauls, the piano's lid
opens, the telephone on the lounge's lamp table rings, the stove's pot
boils over, and 25 or so others. The object's timings give each
reaction's sound: time 1500, 1600, 1700 for states 20, 21, 22, the value
a number into `Clue.exe`'s sound table from its 69th entry
(`bear_g.wav` 0, `bear_b.wav` 1, `rhino_b.wav` 2 ... `mnky_b.wav` 21;
0x42A3F6 adds 0x44 and plays it only below 0x62; the table is a list of
pointers to the names at 0x45E698, 0x5CA98 in the file); the
placement's timing if it has one, else the object's (0x4280B0,
0x428100). Which reaction: one of those the object has, by chance
(0x42A301). Reactions don't touch the game.

When: `Clue.exe` 0x4196A0 handles a click in the view, 0x419600 the
pointer moving. Either does anything only on the turn of the human at
this machine, with no action being carried out, and with their suspect
in one of the rooms (0x428080). Then 0x419980 goes through the hot
spots for the first one that is in the suspect's room (0x42A250
compares the two rooms), isn't still reacting (+0xC8, set as a
reaction starts and cleared as it ends), and whose box's outline on the
screen (the hexagon its corners make, not its picture) holds the point.
On the plan from above it is the box's footprint instead. The two
portraits in the credits painting (`credpnt:Oamespic`, `Oslcpic`) have
a timing at 2100, which makes them react to the pointer passing over
them (+0xC9). The others react to a click, and a click that finds none
is the move's. Reluva does the same.

**Weather.** `Clue.exe` 0x41C690, run every game tick (66 ms; every
path through the tick reaches it): a clock of 66 ms ticks that runs
round a 300 s cycle, in steps. Each step, from the time it starts:
which state the drapes (type 12 objects) are put in (23 blowing, 24
blowing hard, 0 still; each from a frame by chance, 0x428EF0), which
wind loop plays (`wind light gusts.wav`, `wind heavy gusts.wav`, at
volume 100 of 127, or none), and the chance of thunder each tick.

| from | drapes | wind | thunder each tick |
|---|---|---|---|
| 0 s | (as they are) | (as it is) | none |
| 60 s | 23 | light | small, 1 in 100 |
| 75 s | still | none | none |
| 90 s | 23 | light | small, 1 in 100 |
| 120 s | 24 | heavy | middle, 1 in 80 |
| 150 s | (as they are) | (as it is) | a big one as it starts; middle, 1 in 80 |
| 180 s | 23 | light | small, 1 in 100 |
| 210 s | still | none | none |
| 225 s | 23 | light | small, 1 in 100 |
| 240 s | still | none | none |

Small thunder is `SmallThunder6`-`8`, middle `MidThunder1`-`3`, big
`BigThunder4`/`5`, each by chance. Reluva does the same.

A recording of the original ("Clue: Murder at Boddy Mansion gameplay",
YouTube dq0uYzGq2ok, 24:33) has it all: the light gusts and the heavy
ones in stretches (heavy at 4:08-5:20, 10:56-11:54, 17:22-17:32,
20:21-21:19, light around them), the small and middle thunders in them.
Searched for as recorded, none of it is found; as `Clue.exe` plays it,
65/64 fast, all of it is, the gusts' loops matching 0.8-0.9. Every sound
of the game's is played so (below): an earlier search, five recordings
with the files as recorded, found only the suggestions' videos' thunder
(their own sound, at its own speed) and took the weather for thunder
alone.

**How fast sounds play.** `Clue.exe`'s players (0x4330C0 for sounds,
0x4331F0 for streams) set Miles' playback rate to the file's rate × (x +
1) / 64, with x the argument most calls give as 0x40 (it reads like a
pan's middle, but the pan is set apart, 0x433560): 22,050 Hz files play at
22,394, 1.6% faster and higher. Measured in the recording: the gusts,
the thunders, the steps, the voices, the clicks and the menus' thunder at
1.016; the loading's loop, the CD's music and the videos at 1. Reluva
plays the game's sounds at 65/64.

**Over the menus** (0x40C4A0, as each menu with the storm opens):
`mnThndr1.wav` loops, started again only if it isn't playing, and the
storm's four animations (controls 1000-1003: `main_anim:Alt1anim` at
13,239, `Alt2anim` 13,317, `Art1anim` 421,239, `Art2anim` 396,317,
placed by 0x40C520) run. A click on one of those menus (0x40C350, a
control's notification code 0) plays `mnThndr2.wav` once and shows the
lightning, `menu:Alt1flash`, `Alt2flash`, `Art1flash`, `Art2flash`
(controls 1004-1007, at the animations' places), then waits 500 ms
with the screen still and 500 ms more with it going on before the
click is acted on; the screen it opens hides the flashes (0x40C420).
Reluva does the same, and keeps what is pressed during that second for
afterwards, as Windows queues it.

**Pace.** A computer player's turn without a suggestion takes 5-9 s in
the recordings, from one "..., it is your turn." to the next, as it
does with `Clue.exe`'s timings ([playing.md](playing.md#timings)). With a suggestion (its video, the boxes saying who can and
can't disprove), 20-22 s; Reluva's, 20-22 s.

**Footsteps.** A suspect walking steps on the frames of its walk that
have key 5000 (two in each, [res.md](../formats/res.md)): one of
`male_step1`-`5` or `female_step1`-`7` by chance (`Clue.exe` 0x426660).
The walks play at their own speed (13-16 frames a second), so the steps
come as the feet do.

**Sounds of things.** `Clue.exe` 0x42F1C6, each tick: a thing with a
timing of time 1900 makes a sound, its value the sound (0 the
library's fire, `LBfirepl_n.wav`; 1 the conservatory's fountain,
`fntn_n.wav`). For each, the nearest such thing to the point on the
floor under the view's middle is heard: silent 500 or more away (in
the floor's units), else at volume 100 × (1 − distance / 500) of 127,
panned 64 + 52 × dx / distance (0-127, 64 the middle) by how far it is
to the right. Reluva does the same.

## Sources and checks

Source: `clue.lvl` and the objects (`.o`), the readme ("Clickable Items
- ... Click on the bear in the Billiard Room, and you may see him dance,
or you could get mauled!"), and `Clue.exe` 2.2's table of sounds.
