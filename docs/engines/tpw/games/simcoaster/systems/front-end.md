# SimCoaster: the front end

> **Status:** partial · **Code:** `src/engines/tpw/ui/front_end.cpp` · **Checked against:** a recording · **Confidence:** inferred unless marked

## Overview

Between the opening ([opening.md](opening.md)) and the first park, the
game shows its front end: a hall, a boardroom, and a globe of the three
worlds that rises out of the boardroom's table, the advisor's portrait
over them; then the "SC Personnel File" window, where a player is
chosen, and the globe, where a world is
([research/footage.md](../research/footage.md#the-front-ends-opening)).
None of it is a movie: the hall, the boardroom and the globe are one
model of the Intro World, seen through a camera in it.

## How it works

### The model

`levels/wIntro/obstacles/Camera/` (in the Intro World's archives) is an
obstacle its settings call "Camera Path" (`camera.sam`: `Info.Id` 4798,
`Info.IsCameraPath` 1, not choosable, not shown in the game's lists).
Its `camera.MD2`, 144 meshes and a wall of type 0x401
([md2.md](../formats/md2.md#the-nodes)), is all of it:

- the hall (`Room01`, its carpeted floor), its two paintings and its
  double doors (`lobbyleftdoor`, `lobbyrightdoor`);
- the boardroom (`Room`, `ceiling`, `curtains`, `shutters`, a drinks
  cabinet, a plant, the chairs) and its table (`desk`), with the star
  inlaid in it;
- the globe (under the helper `baseobject`), the three worlds' rides on
  it in small, under the table;
- a camera: the helper `CAMERA1`, and under it `Camera01`, turned as a
  3ds Max camera is in the game's axes (its own −y forward, its z up);
- helpers for the effects: `Particle Pivot` and `Fx1` to `Fx4` over the
  table, `sound`, `Dummy01`.

Its main animation, `cameram.MD2` (800 frames;
[md2-animation.md](../formats/md2-animation.md)), moves all of it: the
camera along a path of 801 keys, from the hall through the doors, round
the table and up over it; the doors open from frame 190 to 260 and shut
by 525; the globe rises from frame 500 to 800.

### The script

The obstacle's script, `camera.RSE` ([scripts.md](scripts.md)), waits
for the game to trigger it (`VAR_OBSTACLETRIG`), then starts the main
animation (`TRIGANIM 5`), sets `VAR_SPEECHSTATUS` to 1, 2 and 3 as it
goes (the advisor's speech, it seems *(guessed)*), sends the game events
(`EVENT 5 1 224`, then many `EVENT 2 11 60` to `EVENT 2 14 60`, 200 ms
apart: the sparkles of `Fx1` to `Fx4`, perhaps *(guessed)*), adds an
object (`ADDOBJ 5 1 200 2`) and fades it (`FADEOBJ 2`), waits for the
animation's end (`WAIT4ANIM`) and sets `VAR_OBSTACLEFLAG`.

### Seen in the recording

Set beside the recording's frames, at 25 frames a second from 10 s into
it, when the advisor's portrait appears: the hall at frame 0 is the
recording's at 9 s; the doors stand open, the boardroom beyond, at frame
250 and 20 s; the camera is over the table and its star at 24 s; the
globe rises from 32 s. From 44 s the globe fills the screen and turns,
until the Personnel File opens over it at 1:04: the game turning it, as
the player does later to choose a world *(inferred)*.

The camera's field of view: the recording's hall measured against
Reluva's at frame 0 (the doors' height and width), a focal length of the
screen's height, 53° high *(inferred)*.

The table's middle opens by the animation's vertex track, from frame
500, the globe rising through the lit well beneath it
([md2-animation.md](../formats/md2-animation.md#vertices)).

### The effects

From 23.2 s in the recording, sparkles circle the octagon round the
star, then thin out by 32 s; at 31.5 s a pale-blue beam comes down on
the star and widens into a cone over the opening well, whose walls glow
yellow; from 36 s a white burst lights the well under the rising globe.

- **The beam and the glow** are the model's own meshes: `Cylinder02`
  and `Cylinder04`, textured `BeamTop`, `BeamMain` and `BeamBot`, moved
  and widened by the animation; the desk's `glowinside` and
  `insidewood`. Their groups have flag 1, drawn unlit, and `BeamTop`'s
  texture flag 2, blended by its alpha, which fades the beam out at its
  top ([md2.md](../formats/md2.md#a-mesh)).
- **The sparkles** are particles the script starts: `EVENT 2 <emitter>
  <effect>` starts a particle effect of the library
  ([plb.md](../formats/plb.md)) at one of the model's emitters. The
  script sets off effect 60, "FW1explosion", at emitters 11 to 14 every
  200 ms, 4 seconds and more; then 89, "Upgrade", at emitter 1, six
  times. 11 to 14 are the four helpers `Fx1` to `Fx4` round the star,
  under `Particle Pivot`, which the animation turns: the sparkles trail
  round the octagon as in the recording. How emitters are numbered in
  general isn't worked out: the swamp's script names its 73 "snow
  particle" helpers 30 to 102, the speakers' their one helper
  `Particle` 1 *(guessed: Reluva takes 11 to 14 for `Fx1` to `Fx4`, 1
  for `Particle Pivot`)*.
- **When:** the script's waits put its first sparkles 9.8 s after the
  game sets it going (`VAR_OBSTACLETRIG`); the recording has them 13.2 s
  after the path starts, so the game sets it about 3.4 s into the path
  *(inferred)*. The sparkles' 3-second lives, the library's, match how
  long the recording's linger after the last burst.

What Reluva doesn't match yet: the particles' number, speed, size and
look, which the library's other words must hold (it draws 20 a burst,
small four-pointed stars, flying off at up to 6 units a second,
*guessed*), and the bright flares at the emitters; the white burst in
the well.

### The sounds

The script's other events, `EVENT 5 1 <effect>`, start sound effects by
their numbers in the game's sound categories
([sound-maps.md](../formats/sound-maps.md)): all of them the global
rides' category's, in `global/sound/RideHD.sdt`. Times from the script
being set going, by its own waits:

| Time | Effect | Sound |
|---|---|---|
| 6.2 s | 224 | `barrier_up3`, 1.7 s |
| 8.0 s | 242 | `intro-doorclose`, 0.7 s |
| 13.3 s | 250 | `intro-trapdoor`, 1.2 s |
| 14.0 s | 254 | `magicfx3`, 1.8 s |
| 14.8 s | 250 | `intro-trapdoor` |
| 15.4 s | 245 | `intro-flyin`, 3.2 s |
| 16.7 s, 17.8 s | 252 | `magicfx`, 5.6 s |
| 20.2 s | 253 | `magicfx2`, 5.0 s |

The recording's sound, set beside the sounds by their spectra, has
`magicfx3` at 31.3 s, the second `intro-trapdoor` at 32.8 s and the two
`magicfx` at 34.9 s and 35.9 s, 1.1 s apart as in the script; the others
are lost under the music and the speech. Against the script's times
(the script set going 13.4 s into the recording, as the sparkles put it,
[above](#the-effects)) they're late, by 3.9 s at the first and 4.7 s
from the sparkles' end on: the script ran at about half its speed while
the sparkles flew, then at its own. The recording's computer slowed
under the particles, it seems, the script's waits counting the game's
time but the camera's path the clock's *(inferred)*. Reluva runs the
script by its waits, as a fast enough computer would.

The second value, 1 here and -1 in some rides' scripts, isn't worked
out: where the sound plays from, perhaps *(guessed)*.

### The portrait and the button

Over the scene, until the Personnel File opens, the front end shows the
advisor's portrait at the top right and a round button at the bottom
right. Both are flat models of `ui.wad` ([ui-models.md](../formats/ui-models.md)),
placed as measured on the recording at 640 × 480:

| What | Model, mesh | Where |
|---|---|---|
| The portrait's plate | `!frame` | (503, 8), 129 × 129 |
| The advisor | `mugshot`, its first mesh (`mugshot1`, a bald man in a dark suit on a green glow) | (509, 14), 117 × 117, inside the plate's rim |
| The button, ▷▷\| (skip) | `b_skip` | (570, 410), 64 × 64, its texture's own size |
| The button, \|▷ (play) | `b_play` | the same |

The portrait is a still picture: its face, set beside `mugshot1` in
frames a second apart, doesn't change. It comes and goes, which the
recording times, from the camera's path starting (10 s into it):

| From | To | |
|---|---|---|
| 0.3 s | 25.2 s | The advisor shown |
| 25.2 s | 27.3 s | Gone |
| 27.3 s | 32.1 s | Shown; the path ends at 32 s |
| 32.1 s | 34.1 s | Gone |
| 34.1 s | 52.2 s | Shown, over the globe turning |
| 54 s | | The Personnel File opens; no portrait or button over it |

Three spells, one for each of the advisor's lines
([below](#the-music-and-the-speech)): the portrait shows from 1.2 or 1.3
s before he speaks to 1.3 to 2.8 s after. The script's
`VAR_SPEECHSTATUS` doesn't keep time with them (it turns 1 at 4.4 s, 2
at about 17 s).

The button is skip (▷▷|) until 36.5 s, when the camera has come down
over the globe, then play (|▷). The recording's player never presses
it: the path plays out, and the Personnel File opens 54 s in, 1.8 s
after the advisor's last spell *(inferred: the game opens it once he's
done)*.

### The music and the speech

The recording's sound, set beside the game's banks by their spectra
(each line's first 3 seconds, or 3 seconds of the recording, slid over
every sound), has the advisor saying three lines of the speech bank,
`global/Speech/speechHD.SDT`, and music of the Intro World's own,
`levels/wIntro/Music/MusicHD.sdt`, from the front end's start.

The lines, by their numbers in the speech category (1018 is
`SP_1018`, the bank's 1,018th; [sound-maps.md](../formats/sound-maps.md)),
from the camera's path starting:

| Line | Length | Said from | Matched |
|---|---|---|---|
| 1018 | 20.8 s | 1.6 s | 0.88 (the next best line, 0.38) |
| 1019 | 2.3 s | 28.5 s | 0.80 |
| 1020 | 14.7 s | 35.3 s | 0.78 (the next best, 0.42) |

The game has no text for them (no string table holds the speech's
words). What makes it say them then isn't known: Reluva says them at
the recording's times *(inferred)*.

The music is the Intro World's category's one effect, 2 (`cat_musicSFX.map`):
six groups of pieces, 5 to 29 seconds each, every group linked to all
six. Each link gives a range of 0 to 100: group 1 0 to 10, group 2 11
to 26, 3 27 to 42, 4 43 to 58, 5 59 to 74, 6 75 to 90 (Arabian Nights'
seven groups run on to 100). The game, it seems, picks the next group by
a value of its own, the park's mood perhaps, falling in a link's range
*(guessed)*. In the recording:

- From 8 s (the front end's start, 2 s before the path), pieces of
  group 1: `Level1-102` first (0.95), more after under the speech.
- At 42 s, the path's end, a cut to group 2: `Level2-1663` (0.92), then
  `Level2-637` at 57.1 s, as `Level2-1663` ends (0.91); group 2's on
  through the Personnel File, at 1:16 `Level2-174` or `Level2-637`
  again (0.89, 0.88: much the same music).

Which pieces come in a group is the roll's, so a run differs from the
recording's but in its groups.

## In Reluva

`FrontEndScene` (`src/engines/tpw/ui/front_end.cpp`) loads the camera
path's model and its main animation and draws it, through `Camera01`
posed at a frame, as the viewer draws models
([md2.md](../formats/md2.md#in-reluva)). `playFrontEnd` plays the path
after the opening at 25 frames a second, then the globe turning; over
both, `FrontEndOverlay` (`src/engines/tpw/ui/front_end_overlay.cpp`)
draws the portrait and the button by the times above. The skip button
jumps to 36.5 s, the camera down over the globe (Reluva's guess at what
the game's does); play, or the 54 s mark, opens the Personnel File
([personnel-file.md](personnel-file.md)) over the globe turning; then
choosing a world on it ([globe.md](globe.md)). A key presses the button
too. `runFrontEnd` (`src/engines/tpw/ui/main_menu.cpp`) runs them in
turn.

`FrontEndEffects` plays the script's sounds through `SoundEffects`
([sound-maps.md](../formats/sound-maps.md#in-reluva)), as sound effects
on the engine's mixer; skipping the path stops them. `FrontEndAudio`
(`src/engines/tpw/ui/front_end_audio.cpp`) plays the music, from the
path's start, piece after piece of group 1 by the roll, cut over to
group 2 at the path's end, through the Personnel File and the globe
until the park loads; and the advisor's lines at their times, a line
whose portrait has gone by (the path skipped) not said, his words
stopped when the Personnel File opens. The mixer's music, speech and
effects play at the game's own levels, 60, 75 and 75 percent
(`sound.sam`'s `DefaultVolume`), from after the opening.

`reluva-tool tpw front-end-shot <game folder> <out.png> <frame>` draws
a frame of the path, the portrait and the button over it, and lists the
sounds the script has started by then;
`reluva-tool tpw front-end-run <game folder> [seconds] [-shots
<folder>] [-audio <out.wav>]` runs the whole front end headless, its
sound mixed into a WAV with `-audio`: the skip button clicked so
many seconds in (5), then a key for play, the Personnel File's "Enter
Game" a second later, then a click at the screen's middle every quarter
second until Land of Invention turns under it; with a checksum of the
screens, which the game-files test checks (`tpw.gamedata.frontend`).

## Sources and checks

- **Where the facts come from:** the Intro World's files: the obstacle's
  settings and script, its model's node names and its animation's
  tracks; the sound categories and banks; the recording's front end, from 7.83 s to 1:04.
- **How it was checked:** frames drawn by `front-end-shot` set beside
  the recording's at 9, 14, 20, 24, 32, 38 and 44 s: the hall, its
  paintings (Land of Invention's on the left, Arabian Nights' on the
  right) and its carpet match at 9 s; the doors, the boardroom, the
  chairs and the table's star where the recording has them. The
  portrait and the button at frame 500 set on the recording's at 30 s,
  pixel by pixel: the places above are where they match best.

## Open questions

- The particles' parameters ([plb.md](../formats/plb.md)), and how a
  model's emitters are numbered.
- The white burst in the well; what the script's objects (`ADDOBJ 5 1
  200 2`, `FADEOBJ 2`) are; what `EVENT 5`'s second value says.
- When the game triggers the script, and what `VAR_SPEECHSTATUS` does;
  what makes the advisor say his lines when he does.
- How the game moves the music between its groups (the links' ranges),
  and whether it overlaps a piece's quiet end with the next: Reluva
  plays them end to end, with lulls where a piece fades out.
- What the skip button does in the game, pressed during the path or
  while the camera comes down to the globe; whether the Personnel File
  waits for play or opens by itself.
- The camera's field of view, measured rather than read from the files.
