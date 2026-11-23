# SimCoaster: what a recording of the game shows

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

The game's program can't be read, so what it does with its files is
seen on its screens. Nothing from the recording is in Reluva but
observations and measurements, written down here.

| Recording | Length | Picture | What's in it |
|---|---|---|---|
| "sim coaster theme park inc no commentary #1" (YouTube, `3so8HdYGaL0`) | 24 min 24 s | 1280 x 720, 60 a second; the game 4:3, 960 x 720, between black bars | The game started from the Windows 11 desktop: the logos, the front end, a new player into Land of Invention, its first mission, and about 22 minutes of building and running the park |

It's the US release: its loading screen says "SimCoaster" and it shows
dollars. The game's screen size isn't known; the picture was scaled to
720 lines.

## How

Frames taken with ffmpeg: contact sheets every 2 s for the first four
minutes and every 20 s after, then single frames at full size; the
cuts found with ffmpeg's scene detection (`select='gt(scene,0.3)'`).
Pictures from the game's files were decoded with `reluva-tool tpw
picture-dump` and set beside the frames.

## The opening, second by second

| From | To | What |
|---|---|---|
| 0 s | about 2.4 s | The desktop; the game's icon opened |
| about 2.4 s | 3.73 s | Black, the mouse's wait cursor in the middle |
| 3.73 s | 7.22 s | The EA GAMES logo, its ring turning, on grey: a movie, with sound. One of `Data/movies/bf.tgq` and `bfSim.tgq` *(guessed: both last 3.24 s)* |
| 7.22 s | 7.83 s | The Bullfrog logo, black on white: `Data/Init/<width>/Splash_American.png`, as it is. Only 0.6 s: perhaps clicked past |
| 7.83 s | 1:04 | The front end's opening, drawn in the game's 3D engine (below) |
| 1:04 | 1:14 | The "SC Personnel File" window |
| 1:14 | 1:23 | The globe turning; a world clicked |
| 1:23 | 1:34 | The loading screen |
| 1:34 | 1:50 | The camera flown along the road into the park, to its gates |
| 1:50 | | The mission's briefing, "Operation Expand!", then play |

## The front end's opening

- A room with wood panelling, two pictures and a pair of doors; a
  portrait of a man in a dark suit (the advisor) in a box at the top
  right, from 10 s; a round button at the bottom right, two triangles
  pointing right: skip *(inferred)*.
- The camera goes through the doors into a boardroom: a long table with
  green leather chairs, an inlaid compass star in its middle.
- 24 s to 32 s: sparkles circle the star; a beam of light comes down.
- 32 s to 44 s: a globe rises out of the table through a ring of light
  and fills the screen: the three worlds on one ball, Land of Invention
  (green, a lake), Polar Zone (snow) and Arabian Nights (sand), each with
  its rides. It turns.
- None of it is a movie file: it's the engine drawing the Intro World's
  camera path model (`levels/wIntro/obstacles/Camera/camera.MD2`), the
  hall, the boardroom and the globe in one, through its own camera
  ([front-end.md](../systems/front-end.md)).

## The SC Personnel File

At 1:06 (full frame), over the globe, a window drawn as a light-blue
panel, in the handwriting-style font of `PERSONNELFILE*.bf4` for its
values:

- Tabs for four players: "Enter name here", then three "New Player".
- "Assistant Manager" (the rank), "Golden Tickets : 0".
- "Shareholding % : 3%", "Share Price : $ 100", "Shares Owned : 1000",
  "Portfolio Value : $ 100000".
- A line for each world: "Land of Invention — 0 of 10 challenges
  completed", "Polar Zone — Zone not yet opened", "Arabian Nights — Zone
  not yet opened".
- "Current Objective : Operation Expand!, Land of Invention".
- Buttons "Enter Game", "Load A Game", "Delete Player"; below the panel,
  "Quit Game" and "Coaster Design Kit".
- A woman's portrait at the bottom right (another adviser than the
  room's).

Its words are `UITEXT.str`'s (string 19 "SC Personnel File", 22 "Golden
Tickets :", 31 "Coaster Design Kit", 34 "Enter Game"). 1:12 to 1:18: a
name typed into the first tab; "Delete Player" asks "Are you sure you want
to delete this player?" in a smaller window with a tick and a cross.

## The worlds' names and folders

`THEMENAMES.str` holds, in order: "Land of Invention", "Polar Zone",
"Intro World", "Arabian Nights". `Objects.txt` numbers the themes' objects
"Jungle -- New Science", "Halloween -- Water world", "Space -- Arabian
Nights" (the Theme Park World themes they replace). So the folders are
*(inferred)*:

| Folder | World |
|---|---|
| `levels/science` | Land of Invention |
| `levels/water` | Polar Zone (its rides are on snow and ice) |
| `levels/wIntro` | Intro World |
| `levels/arabian` | Arabian Nights |

## The loading screen

1:23 to 1:34, a still picture: children on a roller coaster car in front
of a Ferris wheel, a share-price graph and a banknote on the right. On
it:

- top left, the "SimCoaster" logo: `Data/Init/<width>/welcome_american.png`;
- bottom right, the legal text in yellow: `Legal_american.png`, the
  same words as `UITEXT.str` string 929;
- top right, the EA GAMES logo; bottom left, QSound's "Q" logo.

The scene is `Data/Init/<width>/welcome.png`, the logos pieces drawn
over it ([loading-screen.md](../systems/loading-screen.md)). The CD's
`Images/Data.lmp` (the installer's) starts with the same scene, 640 x
480, mirrored (the graph on the left) and with a shorter copyright
line.

## The road into the park

1:34 to 1:50: the camera flies low along a road with a white line, past a
tunnel, a monorail and the park's fence, and stops at its gates, golden,
with blue globes on posts. The Intro World's features include a bus, a
ferry and a seaplane (`levels/wIntro/features/`): the visitors' arrival
*(guessed)*.

## Play

From 1:50, the main screen (full frame at 5:00):

- Top left, the money, white digits in a large font, after a symbol drawn
  like `§`: "§ 49485". The price lists show the same `§`, the Personnel
  File a plain `$`. Below it, a thermometer and a yellow face: the park's
  mood *(guessed)*.
- Top right, a compass with two green arrows to turn the view.
- Down the left, round buttons: the pointer, a hand (move the view), a
  bulldozer, a camera, a newspaper.
- Along the bottom, round buttons for buying rides, shops, features
  and facilities, hiring staff, money, the gate, research; an envelope
  with a count (messages); a "?" at the bottom right.
- The windows: "Buy Ride" (tabs by kind; the items with their prices; a
  picture of the ride turning, "Number Already Owned", bars for
  "Excitement rating", "Reliability", "Working life"; a sentence about
  it); "Hire Janitors", "Hire Gardeners", "Hire Entertainers" (candidates
  with their skills and wages); a shop's window; "Visitor Statistics"; a
  visitor's own; "Research Lab".
- A blue creature with a mortarboard (the in-game advisor) appears at the
  bottom right with tips.
- The view is from above at an angle; the camera can go down to the
  ground, by the gates (11:40) and among the visitors (13:40).

## The training video

`TPinc30.avi` (the cabinet's group "Training AVI") is "SimCoaster
Training Video": 3 min 57 s of play at 320 x 240, Cinepak, with the
title on each frame. It isn't in the recording's opening.
