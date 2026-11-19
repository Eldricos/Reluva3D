# Midtown Madness 2: the menus

> **Status:** partial · **Code:** `src/engines/age/ui/front_end.cpp`, `game_menus.cpp`, `race_menus.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

The game's menus, redrawn from its own files: their screens, layout, sounds, and the navigation bar. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

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
  (`vpauditt_lck`...: [races.md](races.md)).
- **`texture/<control>.tga`**: a button's picture, its states one above
  another: four (normal, highlighted, pressed, not available: `main_sp`,
  201 x 232) or five where the description says so (`cruise`: red,
  yellow, green, yellow, dimmed). Reluva draws the first for a button,
  the second for the one with the focus, the third (of five) for the
  event chosen and the last for one not available. `cc_smchk` is three
  (blank, a tick, a cross) and `LOCK` three (open, locked, a tick).
- **`mmlang.dll`**: the text ([mmlang.md](../formats/mmlang.md)).
  Its fonts (Gill Sans MT, Arial Bold) aren't the game's to ship;
  Reluva writes with its own.

## The screens

- **The opening movie**, `logos.avi` (Indeo 5, 320 x 240, 333 pictures
  at 15 a second, with 8-bit sound at 22,050 Hz), first: the whole
  screen kept 4:3 over black, then the main menu; any key or click
  skips it. It's played by Reluva's own Indeo 5 decoder and AVI reader
  (`video::playMovie`, [indeo5.md](../../../../../common/video/indeo5.md)).
- **Splash** (`jpg/splash.jpg`) stands in for the movie where it can't
  be played (the file missing or unreadable), as the game's
  own "No Movie" shortcut leaves it out: any key or click goes on.
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
  ([cruise-and-stunt.md](cruise-and-stunt.md#cruise-and-the-stunt)).
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
  [car-simulation.md](car-simulation.md)). A locked car or colour shows the
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
[multiplayer.md](multiplayer.md#the-menus). The garage is the
single player's, opened from the lobby's SELECT VEHICLE.

## The navigation bar and Quit

Every screen has the navigation bar at the top right, where the
`Navigation Bar` menu of `widget.csv` puts it: **OPTIONS**
(`texture/MNAV_OPT`, five states), help (`mnav_hlp`, three), minimise
(`mnav_sto`, three) and exit (`mnav_ext`, three).

- **Help** shows the game's help file, `MM2HELP.HLP` in its folder (a
  Windows Help file, read by `common/winhelp.cpp`:
  [winhelp.md](../../../../../common/winhelp.md)), in a page of Reluva's own over
  the menu: the topics listed (Enter or a click reads one), and the
  topic's text to scroll (Up/Down, Page Up/Down, the wheel), with its
  pictures in their places (shrunk to fit the page, at most 300 high)
  and its links coloured and underlined. Tab (Shift+Tab) chooses a link,
  Enter or a click follows it, Backspace (or a right click) goes back
  to the topic before; Esc goes back to the list, then closes. A tab
  after a list's bullet goes 4 columns on, else to the second column,
  and a wrapped line goes on under it. The help's fonts aren't used.
  Without the file the button isn't available.
- **Minimise** puts the window away (`System::minimizeWindow`).
- **Exit**, or Esc on the main menu, asks **QUIT?** (`jpg/quit_dlg`, OK
  and Cancel at `quit_dlg`'s places, with the `dlg_ok` and `dlg_can`
  pictures); OK leaves the game.

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
  - Its Instant Replay is the race so far ([replays.md](replays.md#replays)).
    Quitting the replay runs the race on to where it was paused, so it
    can be driven on.
  - Its Options are the Options menus.
- **Enter**, after a race or lesson, opens the game's results screen:
  - `jpg/rshi_bk` "Event Results" or `crshi_bk` "Crash Course Results".
  - In its box: the racers in order with their times (`mmlang.dll`'s
    DNF for those who didn't finish), or the lesson's Pass or Failed and
    its time.
  - Bottom left: the event's or the lesson's description; beside it,
    what the race just opened, in the words of the city's
    `race/<city>/<city>_rewards.csv` ("Congrats!  New paint job for the
    New Beetle RSi!"), as on the game's screens.
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

## Not drawn

These dialogs in `widget.csv` aren't drawn:

- **Setup Serial Communications** (`serial_dlg`) and the MSN Gaming
  Zone's `zone_dlg` ("Connecting to MSN Gaming Zone..."): those
  connections are the past's, and Reluva plays over TCP/IP only.
- **`plim_dlg`** ("You cannot have more than 18 players"): Reluva's
  sessions have 8 at most, which the host's MAX PLAYERS keeps to.
- **Cops & Robbers Settings** (menu 13): its controls' pictures
  (`copr_*`, `crt_*`, `aren_ready`) aren't among the files, nor is a
  background for it. The Host Race Menu has the same choices with
  pictures that are shipped (`free_cr`, `cpsvr_cr`, `robrs_cr` for its
  three games, `none_cr`, `time_cr`, `point_cr` for its limits, and the
  gold mass and limit's fields), and the lobby has the team buttons, so
  this menu looks like an earlier design the released game dropped
  *(inferred)*. Reluva leaves it out rather than draw a screen the game
  likely never shows.
- **`xassign_dlg`**: a Done button and nothing else, no picture and no
  words in `mmlang.dll`, so what it said isn't known. Customize Controls'
  duplicate key has its own dialog, `ctrl_dlg`.

## Control Warning

Control Warning (menu 21) has OK and Cancel where the defaults' dialog
(`odef_dlg`) has them, but no picture (its `join_dlg` isn't shipped)
and no words in `mmlang.dll`. Reluva uses it for something its
Control Options needs: choosing a joystick, pad or wheel from the
keyboard with none connected. It's drawn as a box the size of `odef_dlg`
in `msg_dlg`'s purple, with Reluva's words, "NO GAME CONTROLLER IS
CONNECTED. USE IT ANYWAY?", and the game's OK and Cancel. OK keeps the
choice (one may be plugged in later), Cancel and Esc go back to the
keyboard. What the game itself used it for isn't known *(guessed)*.
- **`MessageBox`** is the game's general message box, `msg_dlg`: it's
  used wherever a message has no picture of its own.

## Sources and checks

- **Where the facts come from:** `tune/widget.csv`, the `jpg/` backgrounds, the buttons' pictures, `mmlang.dll`'s strings ([mmlang.md](../formats/mmlang.md)); the menus seen in public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.game`; the screens compared with the recordings by eye.
