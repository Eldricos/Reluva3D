# Midtown Madness 2: options

> **Status:** done · **Code:** `src/engines/age/game/options.cpp`, `src/engines/age/ui/game_menus.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The Options screens, and how each option is applied. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Options

The game keeps its options in the Windows registry. Reluva keeps them
in its own file, `options.txt` in the game's save folder: lines of a
name and a value (`effectsvolume 0.8`, `key throttle 256 119`), read
when the Race and Drive pages open. See `engines/age/game/options.h`.

| Screen | Background | What's there |
|---|---|---|
| Main Options Menu | `opt_bk` | AUDIO, CONTROL, GRAPHICS (`opt_aud`, `opt_ctl`, `opt_gfx`), ABOUT MIDTOWN MADNESS 2 (`opt_abt`), BACK (`mnav_prev`, picture `mnav_prv`) |
| Audio Options | `aud_bk` | Sound FX, Play Commentary, Play Music, City Sounds (`aud_fx`, `aud_com`, `aud_musc`, `aud_amb`); Device, Stereo FX, Sound Quality (shown, the system's); Sound FX Volume, Music/City Volume, Balance (bars) |
| Graphics Options | `gfx_bk` | Textured Sky, Vehicle Reflections, Show Pedestrians, Smart Rendering (`gfx_sky`, `gfx_rflx`, `gfx_peds`, `gfx_port`); Display (a window, or full screen), Renderer (shown), Resolution; Visibility and Lighting Quality (bars); Texture Quality, Object Detail, Cloud Shadows (None, Low, High: strings 660-662) |
| Control Options | `ctrl_bk` | Auto Reverse, Use POV Hat, Force Feedback (`ctrl_aut`, `ctrl_pov`, `ctrl_fbk`); Controller (the keyboard, a game controller beside it); Steering Sensitivity (a bar); the joystick's dead zone, calibration and force feedback (not available); CUSTOMIZE CONTROLS (`ctrl_cus`) |
| Control Custom | `cuss_bk` | each control and its two keys (`CW Array`) |
| AboutMenu Menu | `about_bk` | the credits, `jpg/credits` (215 x 4,582), scrolling up through their box; DONE (`options_done`, picture `opt_done`) |

Each option screen has DEFAULTS, CANCEL and DONE (`opt_def`, `opt_can`,
and `opt_dn` with the picture `opt_done`). DEFAULTS asks first
(`jpg/odef_dlg`: "Are you sure you want to restore the original
settings?"). CANCEL forgets what was changed on the screen, and DONE
keeps it, saves it and puts it in force. The checks are five-state
pictures: clear, clear and pointed at, ticked, ticked and pointed at,
not available. The focused control's description is the game's picture
for it at the bottom left (`opt_taud`, `aud_tfx`, `gfx_tsky`, `ctl_trev`
and the rest).

What the options do in Reluva:

- **Sound.**
  - Sound FX, Play Commentary and Play Music turn the effects, the
    speech and the music on and off.
  - Sound FX Volume and Music/City Volume are those kinds' levels.
  - Balance moves the whole mix left or right.
  - City Sounds are the ambient sounds of `aud/ambient`. The game's
    description says to check either Play Music or City Sounds, so
    checking one clears the other; both may be off.
- **Graphics.**
  - Textured Sky: the sky dome; without it, the fog's colour.
  - Vehicle Reflections: the game's sphere maps (`texture/refl_dc.tga`
    in the cities, a skyline under a bright sky; `refl_showroom.tga` in
    the garage) added over the racers' and the garage car's shiny parts,
    those whose shader has a shininess (0.5 on body paint; none on glass,
    wheels, the underside or the far detail levels), at 0.6 times it.
    How strong the game's are isn't known: the footage shows the option
    ticked, not its look up close.
  - Show Pedestrians: none at all when off.
  - Smart Rendering ("USE PVS" among `mmlang.dll`'s words): the city's
    `.cpvs` visibility.
  - Display and Resolution: the window, or full screen at 640 x 480 to
    1,600 x 1,200.
  - Visibility: how far the city is drawn, 800 m to 4,000 m, with the
    placed models to 300-900 m.
  - Lighting Quality below half: no pools of headlight on the road.
  - Object Detail: how near a model must be for each finer level of
    detail.
  - Cloud Shadows: `texture/shadmap_day.tga`, a 128-pixel tile that's
    white with a cloud pattern in its alpha (182-255), laid on the city
    from above and multiplied in, so the shadows darken it by up to
    29%; still at Low, drifting with the wind at High. The tile's size
    on the ground (240 m) and the drift (2.5 m/s) are Reluva's. At night
    its counterpart `shadmap_nite.tga` is laid the same way, still: a
    4 x 4 grid of soft pools of light 32 pixels apart (alpha 79-255, the
    ground between darkened to 31-75%), at 116 m to the tile so the pools
    are the street lamps' 29 m apart (`propdefs.csv`). Nothing in the
    files names it, so how the game used it is Reluva's reading: it
    can't follow the lamps themselves, which stand along streets at every
    angle.
  - Texture Quality: the city's textures are halved once at High, twice
    at Medium and three times at Low (to 16 pixels at least), but for
    those whose flag 0x8000 Reluva reads as "keep full detail": the
    cars' backs, the cockpit, the roads, the particles
    ([tex.md](../formats/tex.md)). It takes effect at the next city
    loaded.
- **Controls.**
  - Auto Reverse: the game's description is "your vehicle goes into
    reverse automatically when you accelerate after hitting a building
    or something that won't get out of the way". So the throttle, held a
    second against something without the car moving, backs it away for
    as long as it's held. Only the player's car has it.
  - Use POV Hat: the in-car view first.
  - Force Feedback: a game controller shakes when the car's knocked
    (from 2 m/s of speed lost, fully by 15, for a fifth of a second).
- **A game controller** (any SDL knows as a gamepad), beside the
  keyboard, in Reluva's layout: the left stick steers, the right trigger
  is the throttle and the left the brakes, each by how far it's pushed
  (the stick's middle 15% ignored). The buttons press the controls' keys
  (so rebinding a key moves its button too): A the handbrake, B the horn,
  X reverse, Y the camera, the shoulders the gears down and up, Back the
  map, Start the pause menu, the sticks' clicks the Thrill Cam and
  looking back, the d-pad the next checkpoint, the mirror and looking
  left and right. In the menus the d-pad moves, A and Start choose, B and
  Back go back. Replays keep the sticks' positions with the keys.
  - Steering Sensitivity: how fast the wheels turn to full lock (half
    to one and a half times as fast).
  - The keys: Throttle, Brakes, Steer Left and Right, Handbrake, Horn,
    Shift Up and Down, Reverse, Transmission, Change Camera, Map Zoom,
    Full Screen Map, Map Toggle, HUD Toggle, Rear View Mirror, Next and
    Prev. Checkpoint, Thrill Cam, Look Left, Right, Back and Forward,
    Wide Angle, Dashboard On/Off, Rotating Map, the CD player's four
    (Toggle CD Player, Start/Stop CD, Prev. and Next CD Track), Camera
    Pan, Opponent Position and Enter Chat Msg: all 33 of `mmlang.dll`'s
    (strings 276-309). The list scrolls, 19 at a time. Their keys by default are the
    game's, from its help file's "Driving with a Keyboard": the arrows,
    Space the handbrake, Enter the horn, A and Z up and down a gear, R
    reverse (a stopped car into reverse, or out of it), T the
    transmission, C the camera, E the map's zoom, Q the full-screen map
    (which stops a race alone), Tab the map on and off, H the HUD
    (clock, list, speed, gear, revs and damage), Backspace the rear-view
    mirror, S and X the arrow to the next or the one before of the
    checkpoints left. The help gives no chat key: Y is Reluva's. (Before,
    Reluva's own defaults had WASD as well as the arrows; a saved
    `options.txt` keeps its keys.) The help says Tab toggles "between two
    different sizes of city map, or turn[s] the map on and off"; Reluva's
    only turns it on and off.
  - The rest of the help's table: V the Thrill Cam, the number pad's 4,
    6, 2 and 8 to look left, right, back and forward in the car, W the
    wide view, D the dashboard, F the rotating map, 2-5 the CD player,
    I Opponent Position; Camera Pan has no key in it, so none. What they
    do, where the help doesn't say, is Reluva's:
    - **Thrill Cam**: "view your vehicle from a cool angle while you
      perform a big stunt": a camera stood 35 m ahead of the car, 8 m to
      the side and 3 m up, watching it go by, stood again ahead once the
      car's past it or 80 m off. V again: back to the camera before.
    - **Look Left, Right, Back, Forward**: in the car, the view a quarter
      round, half round, or ahead again; the dashboard goes while
      looking aside.
    - **Wide Angle**: a view 1.35 times as wide, without the dashboard
      ("When you're not in wide screen view, you can press the D key to
      toggle the Dashboard View"). **Dashboard On/Off**: the car's
      dashboard in the car view.
    - **Rotating Map**: the corner map turned with the player, else north
      up. **Map Toggle** (Tab) now goes through two sizes and off: the
      corner's, 1.6 times it (its bottom right corner kept; the help
      doesn't give the second size), none.
    - **Camera Pan**: held, the follow camera goes round the car (1.2
      radians a second), back again after.
    - **Opponent Position**: "This little green arrow indicates your
      opponent's position": a half-size green HUD arrow, lower, to the
      nearest opponent.
    - **The CD player**: the game's `CD_BG` panel (179 x 100,
      "Midtown tek HI-FI STEREO"), its play or stop button lit
      (`CD_PLAY_DN`, `CD_STOP_DN`) and the track's name in its display,
      bottom middle (the game's place for it isn't known). The game's
      player played an audio CD; Reluva's tracks are the game's own
      tunes, the five race tunes then the four cruising ones (Enemy,
      Bullet, Duck, London, PimpHand, SunRoof, TacoLoco, LeTigre,
      BackOff). Start/Stop stops the music or plays the track; Prev. and
      Next go to the track before or after and play it, their buttons
      (`CD_PREV`, `CD_NEXT`) lit a moment.
  - **F1** (not a control the list can change): the controls' list over
    the game, each with its keys; a race alone and the Drive page stand
    still while it's up, as the help says ("in Single Player, the game
    will pause").
- **Customize Controls.** Enter waits for a key for the chosen one of
  the control's two (Left/Right), and Backspace clears it. A key
  already given to another control is refused with the game's
  `jpg/ctrl_dlg`: "Duplicate key or button, please make a different
  selection".

## Sources and checks

- **Where the facts come from:** the options screens' files and pictures; the Graphics Options screen in public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.game`; by hand.
