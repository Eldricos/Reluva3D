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
  Multiplayer and Quick Race (`main_mp`, `main_qck`: shown, not
  available); the driver's box shows the level, Amateur or Professional
  (Left/Right). The highlighted button's description (`mn_cc`...) is in
  the box at the bottom left.
- **Single Race Menu** (`race_bk`): the event (`blitz`, `cp`, `circuit`;
  `cruise` isn't offered: the Drive page is cruising), the race's name,
  laps, opponents, city (LOCATION), time of day and weather (the race's
  own), its pedestrian, traffic and police densities as bars, its map
  (`<city>_map<kind><n>`), the event's description (`race_btz`...), and
  the best place so far. `race_veh` goes to the garage.
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
presses, Esc goes back; a click presses a button, or steps a value (on
its left half back). **U** opens every race, lesson and car.

## Fitting the layout

Most controls are where `widget.csv` puts them. Two things weren't: the
garage's bars (6 high, 187 wide at x 125) sit 18 pixels lower and from x
148, 130 wide, on the grid beside their labels; and the curriculum's
rows and PASS column aren't controls at all. Both were measured on the
backgrounds.

Not drawn: the other 40 menus (options, multiplayer, replays, driver
records, the dialogs), the Vehicle Showcase, and the menus' sounds and
music.
