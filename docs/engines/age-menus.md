# The menus in the Angel Game Engine

How Reluva redraws Midtown Madness 2's menus on the Race page
(`front_end.cpp`). The game builds them from its files: where each
control goes, the pictures it's drawn with and the text. Which screens
lead where, and what each control does, is Reluva's reading of those
files and pictures; the game's program, which ties them together, is
encrypted ([age.md](age.md)).

## The files

- **`tune/widget.csv`**: a heading, then a line a control: the menu's
  name and number, the control's name and number, its x, y, width and
  height on a 640 x 480 screen, and a description ("4 State crash",
  "5 State blitz", " rollover text"). 294 lines over 44 menus.
- **`tune/menu.csv`**: the dialogs' backgrounds and boxes (`Main Menu,
  driv_back,1,0,0,640,480`, `MessageBox,message_box,23,120,202,...`).
  Not used: its background names aren't the `jpg/` files'.
- **`jpg/*.jpg`** in `mm2tex.ar`: the screens' backgrounds (640 x 480:
  `main_bk`, `race_bk`, `ccsf_bk`, `cclon_bk`, `veh_bk`, `splash`...),
  with their labels drawn in; the description pictures shown at the
  bottom left (219 x 69: `mn_cc`, `race_btz`, `sf_cc0`-`sf_cc12`...);
  each race's map (242 x 184: `sf_mapblitz0`); each race's and lesson's
  loading picture (640 x 480: `sf_blitz0`, `sf_crash0`); each car's
  showroom picture (`vpbug_show`) and its locked pictures
  (`vpauditt_lck`...: [age-racing.md](age-racing.md)).
- **`texture/<control>.tga`**: a button's picture, its states one above
  another: four (normal, highlighted, pressed, not available: `main_sp`,
  201 x 232) or five where the description says so (`cruise`: red,
  yellow, green, yellow, dimmed). Reluva draws the first for a button,
  the second for the one with the focus, the third (of five) for the
  event chosen and the last for one not available. `cc_smchk` is three
  (blank, a tick, a cross) and `LOCK` three (open, locked, a tick).
- **`mmlang.dll`**: the text ([mmlang.md](../formats/age/mmlang.md)).
  Its fonts (Gill Sans MT, Arial Bold) aren't the game's to ship;
  Reluva writes with its own.

## The screens

- **Splash** (`jpg/splash.jpg`), first: any key or click goes on. It
  stands in for the opening movie, `logos.avi` (Indeo 5, 320 x 240, 332
  frames at 15 a second, with sound), which isn't played: Indeo 5 has no
  published description to write a decoder from, and the game's own
  "No Movie" shortcut leaves it out too.
- **Main Menu** (`main_bk`): Crash Course (`dvrcc`), Races (`main_sp`),
  Multiplayer (`main_mp`: the multiplayer menus, below) and Quick Race
  (`main_qck`: shown, not available); the driver's box shows the level, Amateur or Professional
  (Left/Right). The highlighted button's description (`mn_cc`...) is in
  the box at the bottom left.
- **Single Race Menu** (`race_bk`): the event (`cruise`, `blitz`, `cp`,
  `circuit`), the race's name, laps, opponents, city (LOCATION), time of
  day and weather (the race's own, in `mmlang.dll`'s words 357-364:
  Morning, Noon, Sunset, Night; Sunny, Cloudy, Foggy, Raining), its
  pedestrian, traffic and police densities as bars, its map
  (`<city>_map<kind><n>`; a cruise's the whole city's, `<city>_maproam`),
  the event's description (`race_btz`..., a cruise's `race_rom`), and
  the best place so far. `race_veh` goes to the garage. Cruise's "races"
  are the cruise, and in San Francisco the game's stunt
  ([age-racing.md](age-racing.md#cruise-and-the-stunt)).
- **Crash Course** (`ccsf_bk`, `cclon_bk`): the city in the heading's
  box, the lesson, its description (`sf_cc<n>`, `lon_cc<n>`), and ticks
  in the PASS column of the curriculum on the background for the lessons
  passed (its rows measured on the picture). Done (`host_dn`) goes to
  the garage. The "work experience" races and the intro screen with its
  city buttons (`cci_sf`, `cci_lon`) aren't drawn.
- **Garage** (`veh_bk`, "Select Vehicle"): the car (turning, drawn in 3D
  in the window at the top), its colour and gearbox, and the "relative
  performance scores": each car's `.info` Horsepower, Top Speed,
  Durability and Mass as bars against the most of any car (these are the
  numbers the menus show, not the simulation's:
  [age-driving.md](age-driving.md)). A locked car or colour shows the
  `LOCKED` sign and its locked picture; an open one its description
  (`texture/<CAR>_DESC`, stored upside down). `vesel_godrive` starts.

Up/Down move between the controls, Left/Right change a value, Enter
presses, Esc goes back. The mouse works as in the game: pointing at a
control chooses it (and says its word), a click presses a button, or
steps a value (on its left half back). The same goes for the Options and
the dialogs, and in the race for the in-race menu, the results screen's
buttons and the replay's bar. **U** opens every race, lesson and car.

## Fitting the layout

Most controls are where `widget.csv` puts them. Two things weren't: the
garage's bars (6 high, 187 wide at x 125) sit 18 pixels lower and from x
148, 130 wide, on the grid beside their labels; and the curriculum's
rows and PASS column aren't controls at all. Both were measured on the
backgrounds.

## Multiplayer

The Net Select Menu (sessions), Enter an Address, Enter a valid
password, Host Options, the Host Race Menu, the Network Arena (the
lobby) and the eject dialog, with the message boxes for joining, being
ejected and the host quitting. Which picture is which control, and the
boxes measured on the backgrounds, are in
[age-multiplayer.md](age-multiplayer.md#the-menus). The garage is the
single player's, opened from the lobby's SELECT VEHICLE.

## The navigation bar and Quit

Every screen has the navigation bar at the top right, where the
`Navigation Bar` menu of `widget.csv` puts it: **OPTIONS**
(`texture/MNAV_OPT`, five states), help (`mnav_hlp`, three), minimise
(`mnav_sto`, three) and exit (`mnav_ext`, three).

- **Help** shows the game's help file, `MM2HELP.HLP` in its folder (a
  Windows Help file, read by `common/winhelp.cpp`:
  [winhelp.md](../formats/winhelp.md)), in a page of Reluva's own over
  the menu: the topics listed (Enter or a click reads one), and the
  topic's text to scroll (Up/Down, Page Up/Down, the wheel). Esc goes
  back, then closes. The help's pictures, links and fonts aren't shown.
  Without the file the button isn't available.
- **Minimise** puts the window away (`System::minimizeWindow`).
- **Exit**, or Esc on the main menu, asks **QUIT?** (`jpg/quit_dlg`, OK
  and Cancel at `quit_dlg`'s places, with the `dlg_ok` and `dlg_can`
  pictures); OK leaves the game.

## Options

The game keeps its options in the Windows registry. Reluva keeps them
in its own file, `options.txt` in the game's save folder: lines of a
name and a value (`effectsvolume 0.8`, `key throttle 256 119`), read
when the Race and Drive pages open. See `engines/age/options.h`.

| Screen | Background | What's there |
|---|---|---|
| Main Options Menu | `opt_bk` | AUDIO, CONTROL, GRAPHICS (`opt_aud`, `opt_ctl`, `opt_gfx`), ABOUT MIDTOWN MADNESS 2 (`opt_abt`), BACK (`mnav_prev`, picture `mnav_prv`) |
| Audio Options | `aud_bk` | Sound FX, Play Commentary, Play Music, City Sounds (`aud_fx`, `aud_com`, `aud_musc`, `aud_amb`); Device, Stereo FX, Sound Quality (shown, the system's); Sound FX Volume, Music/City Volume, Balance (bars) |
| Graphics Options | `gfx_bk` | Textured Sky, Vehicle Reflections (not available), Show Pedestrians, Smart Rendering (`gfx_sky`, `gfx_rflx`, `gfx_peds`, `gfx_port`); Display (a window, or full screen), Renderer (shown), Resolution; Visibility and Lighting Quality (bars); Texture Quality (shown), Object Detail, Cloud Shadows (shown) |
| Control Options | `ctrl_bk` | Auto Reverse, Use POV Hat, Force Feedback (not available) (`ctrl_aut`, `ctrl_pov`, `ctrl_fbk`); Controller (the keyboard); Steering Sensitivity (a bar); the joystick's dead zone, calibration and force feedback (not available); CUSTOMIZE CONTROLS (`ctrl_cus`) |
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
- **Controls.**
  - Auto Reverse: the game's description is "your vehicle goes into
    reverse automatically when you accelerate after hitting a building
    or something that won't get out of the way". So the throttle, held a
    second against something without the car moving, backs it away for
    as long as it's held. Only the player's car has it.
  - Use POV Hat: the in-car view first.
  - Steering Sensitivity: how fast the wheels turn to full lock (half
    to one and a half times as fast).
  - The keys: Throttle, Brakes, Steer Left and Right, Handbrake, Horn,
    Shift Up and Down, Reverse, Transmission, Change Camera, Map Zoom,
    Full Screen Map and Enter Chat Msg, named by `mmlang.dll`. Reverse
    (by default B) takes a stopped car into reverse, or out of it.
- **Customize Controls.** Enter waits for a key for the chosen one of
  the control's two (Left/Right), and Backspace clears it. A key
  already given to another control is refused with the game's
  `jpg/ctrl_dlg`: "Duplicate key or button, please make a different
  selection".

## The Crash Course's intro and the Vehicle Showcase

- **Crash Course Intro** (`jpg/ilon_bk`): Uncle Donald's two schools,
  first when the Crash Course is chosen. LONDON CABBIE (`cci_lon`) and SF
  STUNT DRIVER (`cci_sf`) choose the city's school; BACK is the
  navigation bar's (the screen's own row in `widget.csv` is garbled:
  `2,mnav_prev,4,1,290,415,90`). The Crash Course screen's background
  names its school, so the city isn't written over its heading any more
  (where the navigation bar now is).
- **Vehicle Showcase** (the garage's VEHICLE SHOWCASE, `veh_show`): the
  car alone, turning, the whole screen, with its name and description;
  DONE (`opt_done`) goes back. The game's `texture/VSDE_*` pictures
  (Volkswagon Beetle, Bus, Cadillac Eldorado...) are mostly only the
  car's name; the garage's fuller `<CAR>_DESC` is shown instead.

## In the race

- **Esc** stops the race: the game's in-race menu (`mmlang.dll`'s MAIN
  MENU: Resume Driving, Restart Race or Restart Lesson, Instant Replay,
  Options, Quit to Race Menu), over the race darkened. The game's
  picture for it isn't among the files, so its words are drawn as they
  are.
  - Its Instant Replay is the race so far ([age-racing.md](age-racing.md#replays)).
    Quitting the replay runs the race on to where it was paused, so it
    can be driven on.
  - Its Options are the Options menus.
- **Enter**, after a race or lesson, opens the game's results screen:
  - `jpg/rshi_bk` "Event Results" or `crshi_bk` "Crash Course Results".
  - In its box: the racers in order with their times (`mmlang.dll`'s
    DNF for those who didn't finish), or the lesson's Pass or Failed and
    its time.
  - Bottom left: the event's or the lesson's description.
  - Down the right, the game's `texture/RESULT_*` buttons (four states:
    blue, red pointed at, yellow pressed, grey not available): Restart
    Race, Next Race (when the next is open, and for a lesson when this
    one's passed), Instant Replay, Race Menu, Exit to Windows (which asks
    Quit?). The lessons use the same pictures; there are none of their
    own.

## The menus' sounds

- **Music.** The menus play the game's menu tune: the segment
  `aud/dmusic/csv_files/ui.csv` names (`UI.sgt`), by Reluva's
  DirectMusic player.
- **Voices.** Pointing at a button says its word, as the game's
  `aud/aud22/UI*.wav` recordings name them: RACES (`UIraces`),
  MULTIPLAYER (`UImulti`), INSTANT REPLAY (`UIreplay`), RACE RECORDS
  (`UIrecords`), the garage (`UIvehicles`), OPTIONS (`UIoptions`), GO
  DRIVE (`UIgo`) and the driver (`UIdriver`). They play as speech, so
  Play Commentary turns them off.
- **Times and weathers.** Choosing a time of day or a weather in Change
  Race Options says it: `UImorning`, `UInoon`, `UIsunset`, `UInight`;
  `UIsunny`, `UIcloudy`, `UIrain`. There's no recording for fog, and
  `UIsnow` is for a weather the game hasn't (like `mmlang.dll`'s unused
  "Weather: Snowing", 411).
- **Alerts.** The dialogs that say no (You cannot pick a locked vehicle,
  You cannot delete your only driver, a driver's name or a key already
  taken) sound the game's `uialert` (`aud/aud11/uialert`).

## Drivers and records

The main menu's **SELECT DRIVER**: the driver's name and level in its
box (Left/Right goes to another), and every driver in the grid below it.
Each driver has their own level (Amateur or Professional, chosen when
they're created) and their own record
([age-racing.md](age-racing.md#the-record-and-unlocking)). The buttons
beside it (`texture/DVRNEW`, `DVRDEL`, `DVRSTS`, `RACE_REC`) open:

- **Create a New Driver** (`jpg/newp_dlg`): the name, AMATEUR or
  PROFESSIONAL (`texture/Checkbox`), CANCEL and DONE. A name already
  taken (whatever its letters' case) gets the game's `jpg/dupp_dlg`:
  "The driver name you entered already exists. Please enter a unique
  driver name."
- **Delete This Driver**: the game's `jpg/delp_dlg`, "Are you sure you
  want to delete this driver?" (YES and NO). The only driver can't be
  deleted: `jpg/lstp_dlg`.
- **Driver's Stats** (`jpg/drec_dlg`): the driver's best place and time
  in each race at each level. Its checks (`texture/dlg_chkb`) choose the
  events and the cities.
- **Race Records** (`jpg/hoff_dlg`): each race's best time at each level
  among all the drivers, and whose it is. Its checks choose amateur
  times, professional times, the events and the cities. The game's
  professional points aren't kept by Reluva, so that check isn't
  available.

Two controls in each of these menus share a name in `widget.csv` (two
`checkbox`es, five and eight `popup_checkbox`es). They're told apart by
their order. The stats' and records' dialogs are 460 high, so they go
at (50, 10) rather than `menu.csv`'s (50, 37).

**Quick Race** (`main_qck`, "Quick Race starts you in the last race you
ran") goes to the garage for the driver's last race, then starts it.

## Changing a race

The Single Race Menu's own boxes hold the race's conditions, as its
file has them. They can be changed:

- **LAPS** (a circuit's) and **OPPONENTS** (up to as many as the race
  has paths for): Left/Right.
- **CHANGE** (`texture/race_chg`, 25 x 122 a state, five states): the
  game's **Change Race Options** (`jpg/Renv_dlg`, at `menu.csv`'s 120,
  52). It holds:
  - The time of day and the weather, each a row of the game's icons
    (`texture/RENV_*`) in the picture's box for it, the chosen one lit,
    chosen with Left/Right or a click.
  - The traffic, pedestrian and police densities, as bars on its grid
    (a tenth of traffic or pedestrians a step, a police car a step,
    0-4).
  - **Hide Pedestrians** (`RENV_HIDE`).
  - DONE (on its left) and CANCEL (on its right), as `widget.csv` places
    them.

A race whose conditions are changed shows "Changed: not recorded" and
isn't kept in the record, as the game's records say: "The race
statistics are recorded for races under default conditions only."
Replays keep the conditions they were run in. Next Race goes back to
the next race's own.

Three things here aren't as `widget.csv` has them:

- `race_chg` has no row in it: its place is measured, up the right of
  the time, weather and densities.
- Its icons (`renv_imorn`... at 208-376, 40 and 82) sit on the picture's
  title. The picture has a box for each row instead, so they go in the
  boxes.
- The icons' names (sun, fog, rain, snow) don't match their pictures (a
  sun, a cloud, rain, a sun behind a cloud). They're placed by picture,
  in the order the lightings number the weathers ([tune.md](../formats/age/tune.md)):
  clear, cloudy, foggy (the sun behind a cloud), rainy.

**LOCATION**: Enter opens the game's **Race Locales** (`jpg/cenv_dlg`,
"Choose race locale"), the cities to choose from. Left/Right still steps
through them.

**A locked car**: GO DRIVE gets the game's `jpg/lock_dlg`, "You cannot
pick a locked vehicle".

## Not drawn

These dialogs in `widget.csv` aren't drawn:

- **Setup Serial Communications** (`serial_dlg`) and the MSN Gaming
  Zone's `zone_dlg` ("Connecting to MSN Gaming Zone..."): those
  connections are the past's, and Reluva plays over TCP/IP only.
- **`plim_dlg`** ("You cannot have more than 18 players"): Reluva's
  sessions have 8 at most, which the host's MAX PLAYERS keeps to.
- **Cops & Robbers Settings** (menu 13): its controls' pictures
  (`copr_*`, `crt_*`, `aren_ready`) aren't among the files, nor is a
  background for it. The Host Race Menu's Cops & Robbers panel and the
  lobby's team buttons do its work.
- **`xassign_dlg`** and **Control Warning**: no pictures (Control
  Warning's `join_dlg` isn't shipped). Customize Controls' duplicate key
  has its own, `ctrl_dlg`.
- **`MessageBox`** is the game's general message box, `msg_dlg`: it's
  used wherever a message has no picture of its own.
