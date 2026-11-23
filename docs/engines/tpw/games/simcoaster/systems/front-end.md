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

What the recording shows that Reluva doesn't draw yet: the sparkles
round the star, the beam of light, the ring of light the globe rises
through and the hole it leaves in the table, the advisor's portrait,
the skip button (bottom right, two triangles and a bar).

## In Reluva

`FrontEndScene` (`src/engines/tpw/ui/front_end.cpp`) loads the camera
path's model and its main animation and draws it, through `Camera01`
posed at a frame, as the viewer draws models
([md2.md](../formats/md2.md#in-reluva)). `playFrontEnd` plays the path
after the opening at 25 frames a second; a key or a click skips to its
end, as the game's skip button does. Then the Personnel File
([personnel-file.md](personnel-file.md)) over the globe turning, and
choosing a world on it ([globe.md](globe.md)); `runFrontEnd`
(`src/engines/tpw/ui/main_menu.cpp`) runs them in turn.

`reluva-tool tpw front-end-shot <game folder> <out.png> <frame>` draws
a frame of the path; `reluva-tool tpw front-end-run <game folder>
[seconds] [-shots <folder>]` runs the whole front end headless: the
path skipped so many seconds in (5), the Personnel File's "Enter Game"
clicked a second later, Land of Invention on the globe a second after
that; with a checksum of the screens, which the game-files test checks
(`tpw.gamedata.frontend`).

## Sources and checks

- **Where the facts come from:** the Intro World's files: the obstacle's
  settings and script, its model's node names and its animation's
  tracks; the recording's front end, from 7.83 s to 1:04.
- **How it was checked:** frames drawn by `front-end-shot` set beside
  the recording's at 9, 14, 20, 24, 32, 38 and 44 s: the hall, its
  paintings (Land of Invention's on the left, Arabian Nights' on the
  right) and its carpet match at 9 s; the doors, the boardroom, the
  chairs and the table's star where the recording has them.

## Open questions

- The effects: the sparkles, the beam, the ring of light, the globe's
  hole in the table; what the script's events and objects are.
- When the game triggers the script, and how the advisor's speech
  follows `VAR_SPEECHSTATUS`.
- The camera's field of view, measured rather than read from the files.
