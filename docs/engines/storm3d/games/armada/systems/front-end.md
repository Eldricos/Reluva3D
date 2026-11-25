# Star Trek: Armada: the front end

> **Status:** partial · **Code:** `src/engines/storm3d/shell/` · **Checked against:** the original running in Reluva's x86 emulator, its frames, the files it reads and its code · **Confidence:** seen, not traced unless marked

## Overview

Before a game the program shows its front end: the opening movie, then
the main menu, and from it the single-player screens, multiplayer's
setup, the options, the credits. Each screen is a modal dialog of the
program's own, which it draws all of itself: a background bitmap, Bink
movies in panels, and labels from bitmaps
([research/startup-trace.md](../research/startup-trace.md#the-files-it-opens-in-order)).
`shell_cfg.h` has buttons' places for some screens, but not the main
menu's as it draws them: those are in the program. The places below were
found by matching the bitmaps and the movies' frames against the
original's frames from the emulator (the host, [hosting.md](hosting.md)).

## How it works

### The opening movie

The program plays `animations\stintro.bik` (looking for it in the
install first, then on the CD's drive, as `setup\animations\stintro.bik`,
where the installer leaves it; [formats/movies.md](../formats/movies.md)),
320 × 240 shown over the whole 640 × 480 screen, doubled. Esc ends it.

### The main menu

Over `bitmaps\main\mainbk.bmp`, four panels each play a movie, 15
frames a second, over and over, and six labels are drawn, each from
three bitmaps (`bitmaps\ms\<name>1.bmp` to `3`), magenta (255, 0, 255)
left out:

| Button | Label, at | Panel's movie, at |
|---|---|---|
| Single Player | `MSsingl` (22, 10) | `Single01.bik`, 226 × 180, (0, 45) |
| Multiplayer | `MSmulti` (488, 10) | `Multi01.bik`, 226 × 174, (353, 32) |
| Options | `MSoption` (22, 452) | `Const01.bik`, 370 × 160, (0, 285) |
| Credits | `MScredit` (513, 433) | none |
| Quit | `MSquit` (544, 415) | `Credit01.bik`, 200 × 200, (408, 217) |
| Replay Intro | `MSintro` (439, 451) | none |

The labels' places fit the frame to within 2.5 of 255 a pixel; the
panels' place their movies' frames on it. A button is its label and its
panel: with the pointer over the construction yard the game lit
Options, and over the ship flying off (the movie called `Credit`) it lit
Quit, and a click there ended the program. The label under the pointer is drawn from its second bitmap;
the third, by the order, is for it pressed *(guessed)*. The panel under
the pointer plays its second movie (`Single02.bik`...), its frames
matching it, 0.88 against 0.84 for the first *(inferred)*.

In the bottom right the program writes its version, "v1.0", red (181, 0,
0 as shown), from (605, 467), in GDI's "System" font, bold
(`DrawTextExA`).

Its sounds: as it opens, `sounds\effects\shellsd0.wav`, three minutes of
music, played over and over, `PlaySoundA`'s flags 0x201f having
`SND_LOOP` (0x546195) *(from the code)*; as the pointer comes onto
a button, `cornron.wav`; a click, `cornrclk.wav`.

### The single-player screen

Single Player opens it over `bitmaps\single\singlebk.bmp`: its title
(`bitmaps\sp\SPtitle.bmp`) at (256, 12), Previous Menu (`SPprev1` to `3`)
at (28, 451), Load Game (`SPload`) at (507, 452), and a panel for each
race's campaign, 280 × 190, playing `animations\<race>01.bik` (`02`
under the pointer, its emblem lit): the Federation at (0, 30), the
Klingons at (360, 30), the Romulans at (0, 250), the Borg at (360,
250). Their movies, matched against the frame, sit there to a
correlation of 0.998 or more. `Omega01.bik` (80 × 220), the finale's, is
read but not shown to a new player.

The campaigns open one after another, the races in that order, each
when the one before it is finished, as the player's progress in
`shell.set` says ([formats/shell-set.md](../formats/shell-set.md)). The
pointer over a race not open yet writes, centred on its panel, white,
in Arial Bold of 16 pixels, the last campaign open, to finish first:
for a new player,
"Federation campaign not complete" over each of the others
(`label.map`'s `single_player` section,
[formats/label-map.md](../formats/label-map.md)).

Clicking an open race opens the mission panel, a dialog the program
moves to (224, 46): `SPpanel.bmp`, its heading (`SPselect`, "Select
Mission") at (243, 75), a bar for each mission open (`SPgame1` to `3`,
at (234, 146), then 27 further down each), and O.K. and Cancel (`SPok1` to `3`) at (228,
386) and (328, 386). The missions' names and the buttons' words are
text, black, in Arial Bold of 12 pixels, centred on their bars: the
names are `label.map`'s `mission_select` entries for each map, the maps
`mshell.set`'s, six lines a campaign (`none` for none; four missions
each, and the finale's). A new player has the first open: the
Federation's is "Premonitions" (`fed1.bzn`). With three Klingon
missions open, the panel opened with none of them chosen. The races'
movies stop while the panel is up, the one chosen on its lit movie.

### The Options screen

Options on the main menu opens it over `bitmaps\wonlogin\wlsBG.bmp`,
which has the title bar and the panel painted in. Three buttons,
`wonlogin\wls324tg1` to `3` (324 × 32), at (158, 192), (158, 232) and
(158, 272), say "Graphics Settings", "Sound Settings" and "Game
Settings", black, in Arial Bold of 16 pixels, centred. The title,
"OPTIONS", is white, in Arial Bold of 20 pixels, its line's top at
(65, 6). The words are `label.map`'s `escape_menu` section's, the menu
Esc opens in play, which has these three among its own.

Along the bottom are three plates, all from `bitmaps\multiset\`:

- Previous Menu (`navback1` to `3`, 112 × 30) at (5, 445), in black,
  16 pixels.
- A blank `mpsstart1` (394 × 30) at (123, 445).
- A blank `navmain1` (112 × 30) at (522, 445).

All of these have the front end's magenta colour key.

### The settings screens

Each of the three buttons opens a settings screen over
`bitmaps\credit\creditsBG.bmp`, with the Options screen's bottom bar.
The title is white, in Arial Bold of 20 pixels, at the top as on the
Options screen (Graphics Settings' a little further left, at x 59). The
labels are yellow, (255, 190, 0) as drawn, in Arial Bold of 16 pixels
(Graphics Settings' two column headings 20), centred over their
controls, or in a column centred at x 215 left of the switches. All the
words are `label.map`'s `graphics_settings`, `sound_settings` and
`game_settings` sections'.

The controls are of three kinds:

- **Switches,** `multiset\nav3but1` to `3` (126 × 30). A click turns
  them to their next value ("On" to "Off"), its words black, 16 pixels,
  centred.
- **Sliders,** `holder\scrollbar.bmp` (224 × 20), − and + at its ends.
  The value is a blue (0, 0, 255) line a pixel high, 10 down and 27 in,
  170 long at most, so many pixels a unit of its value. A click on − or
  + takes a step off or adds one.
- **Lists,** `wonlogin\wls324pd1` to `3` (324 × 32), the value's words
  12 pixels, centred. The program reads `pdm324top`, `pdm324hl2` and
  `3` for the list opened, which a click didn't open in the frames.

The values are the player's preferences, `ARMADA.PRF`, which the
program writes as it quits ([formats/preferences.md](../formats/preferences.md),
with each slider's steps and lengths):

| Screen | Control | Place | Value in the frames |
|---|---|---|---|
| Graphics | Display Mode* (list) | (20, 116) | 640x480x16 |
| Graphics | Display Driver (list) | (20, 166) | the machine's |
| Graphics | Use Alternate Font | (386, 116) | Off |
| Graphics | Cinematic Window | (386, 166) | On |
| Graphics | Graphics Detail (slider) | (338, 216) | 168 |
| Sound | Music, Effects, Voice Volume (sliders) | (208, 80), (208, 130), (208, 180) | 170 |
| Sound | Sound Channels | (340, 214) | 8 |
| Sound | Stereo Sound, Unit Speech, Alert Audio | (340, 254), (340, 294), (340, 334) | On |
| Game | Game Difficulty | (340, 64) | Medium |
| Game | Game, Mouse Scroll, Keyboard Scroll Speed (sliders) | (208, 130), (208, 180), (208, 230) | 84, 80, 28 |
| Game | Mouse Button Interface | (340, 264) | Right Click |
| Game | Mouse Camera Control | (340, 314) | On |
| Game | Directors Cut | (340, 364) | Off |

A label above its control has its line's top 16 above the control's. Graphics Settings also says, at the bottom of its panel,
"\* Some resolutions may not be supported by your hardware."

### The Multiplayer Connection screen

Multiplayer on the main menu opens it over the Options screen's
background, `bitmaps\wonlogin\wlsBG.bmp`, with the same bottom bar. Its
title, "MULTIPLAYER CONNECTION", is white, 20 pixels, from (64, 6).
"Enter Name:" is white, 16 pixels, from (158, 78), over the name's box,
`wonlogin\wlstext.bmp` (324 × 31), at (158, 96). Three `wls324tg`
buttons at (158, 162), (158, 228) and (158, 294) say "Internet", "Local
Area Network (TCP/IP)" and "Instant Action" (`label.map`'s
`multiplayer_connection`). Unlike the Options screen's, they rest in
their second look and light in their third under the pointer, and their
words and Previous Menu's are dark grey, (49, 49, 49) as drawn.

Instant Action opens the Game Setup screen: the players and their sides
and teams, the map, and the game's options, with LAUNCH. (A run that
had pressed Esc over the main menu first left this screen unanswering;
without it, the screen works.)

### The Game Setup screen

Instant Action opens it over `bitmaps\multiset\mpsBG.bmp`, which has
its panels and boxes painted in. Its title, "GAME SETUP", is white, 20
pixels, from (64, 6); its headings ("Player Name", "Side", "Teams",
"Game Name", "Messages") yellow, 16 pixels. Its words are `label.map`'s
`multiplayer_setup`.

- **Eight slots,** 19 apart from y 54, each three lists: who
  (`mpsplapd`, 145 × 19, at x 5), their side (`mpsracpd`, 111 × 19, x
  150) and team (`mpsteapd`, 58 × 19, x 261), the words black, 12
  pixels, centred. The game opens it with the player (their name in
  red, (255, 0, 0)), a computer at medium and six empty slots, every
  side Random, the computer on Team 2. Before a name, a bullet (•, 20
  pixels) in that player's colour: white for the player, red for the
  computer.
- **The colours,** nine swatches 27 × 16, 29 apart from (16, 209):
  `teamcolor.odf`'s empty, white, red, blue, green, yellow, purple,
  cyan and brown. One a player has is dark, (25, 24, 25); the first
  is framed in black.
- **The messages' panel,** its scroll bar's arrows at (325, 250) and
  (325, 377), its thumb at (325, 271); who they go to, "ALL", in a
  `mpsteapd` list at (563, 421).
- **The map,** its file's name and how many it takes (its `Camera_<n>`
  paths: "4 Players"), yellow, 16 pixels, centred over its picture's
  place. The game opens it on `4first.bzn`, "The Very First Map".
- **The options,** fourteen 111 × 20 controls down the right, 24 apart
  from (524, 60), and five 128 × 20 below the map, 23 apart from (385,
  272). Lists (`mps111pd`, `mps128pd`, an arrow at their right end)
  for the game's type, its most players, the ping filter, the starting
  dilithium, crew and officers ("Dilithium:   3000", three spaces),
  the units, the technology, what the map shows and the shroud and fog;
  toggles (`mps111tg`, `mps128tg`) for the rest and SELECT MAP. A few
  of `label.map`'s keys name the other state: `ferengi_off` says
  "Ferengi On".
- **The bottom bar,** a pixel right and down of the other screens' (its
  right plate two), saying Previous Menu, LAUNCH (centred on its whole
  plate) and Main Menu.

### The Load Game screen

Load Game on the single-player screen opens it over
`bitmaps\misc\LoadBG.bmp`, its title "LOAD GAME" white, 20 pixels, from
(64, 6). Beside the list's panel is a scroll bar: `multiset\scrollup1`
(14 × 21) at (614, 53), its thumb `scrolltb1` (14 × 10) at (614, 74),
and `scrolldn1` (14 × 20) at (614, 396). The bottom bar's middle plate
says "Load Game", centred in 384 of its 394 pixels, and its right one
"Delete Game" (`label.map`'s `load_game`).

The program lists the saved games with `_findfirst` and `_findnext`; it
names them `save\s<n>.sav`, by its strings *(inferred)*. With no saves,
or with files there that aren't saves, the list is empty. Saves are made
in play, so what they hold, and how the list shows them, isn't known
yet.

### The credits

Credits on the main menu shows the credits, drawn in 3D, until a key or
a click ends them:

- The camera is at the origin, looking along +z, 45 degrees high, as on
  the loading screen. Around it is the stars' background model,
  `mbgstars.sod`, still.
- The text is `Credits.txt`'s pages, one at a time
  ([formats/credits.md](../formats/credits.md)). Each letter is a node
  of `font.sod`, named by its character's code ("65" for A). The glyphs
  are flat, in a strip along x, their lower-case feet at y 84.4.
- At the top left is "Press any key to exit", in the sprite font
  (`font.spr`, [formats/spr.md](../formats/spr.md#fonts)). Its letters
  are yellow, a pixel apart from (5, 5), over a box from (5, 5) to (119,
  15) in (80, 0, 48). They're dimmer than the texture: Direct3D samples
  the sheet where its texels meet, so each pixel is the mean of four.

From the original's draws of pages held at scales 0.8 and 0.5:

- A page's letters lie flat, facing the camera, all at one depth
  whatever the scale.
- A font unit is 3.084 pixels on the screen times the page's scale:
  2.467 at 0.8, 1.542 at 0.5.
- After each letter's own width comes a gap of 7.19 pixels at either
  scale; a space is 7 units, scaled.
- Each line is centred on x 319.5, its last gap counted.
- The lines are 7.19 pixels and 10.755 scaled units apart (33.71 pixels
  at 0.8, 23.76 at 0.5), blank ones too. The block is centred down the
  screen, its middle baseline 0.67 + 8.72 × the scale pixels below y
  239.5, a line through the two scales *(inferred)*.
- The letters' colour is the page's, lit: 0.502 + 0.5255 × the script's
  colour. The first page's (0, 0.3, 0.6) is drawn as (128, 168, 208).

From frames every 100 ms of the program's clock, each page's turn is:

1. 3 seconds of nothing;
2. a second fading in, linearly;
3. the page's hold;
4. 3 seconds going away from the camera at a steady speed, so its size
   on the screen is 1 / (1 + seconds), about the screen's centre. The
   last of these seconds fades it out.

The next page's turn follows at once. Under the emulator the first
page's turn began 3.2 seconds after the click, as the files loaded.

### The loading screen

O.K. on the mission panel plays `cornrclk.wav` and shows the loading
screen while the mission loads (its map, every object's definition and
model, the computer players). There's no briefing screen between: run
past O.K. under the emulator, the program never opened the
`single_briefing` pictures its strings name (`spmainbc.bmp` and the
like), and the mission opened on a cut scene in the game's own 3D, a
ship passing a planet, to `sounds\music\Mfed25.wav`. What the briefing
pictures are for isn't known. It's drawn in 3D: the program's camera at
the origin, looking along +z, 45 degrees high, as in play
([camera.md](camera.md)), with no ambient light and none of the map's.

- Around the camera, the Baku nebula's background model, `mbgbaku.sod`,
  drawn as a map's background is ([formats/bzn.md](../formats/bzn.md#lighting)).
- 35 ahead, unturned, the game's logo, `stalogo.sod`: its draws' vertices,
  placed back through the camera (a depth of 1.208 / rhw), fall on the
  model's 28.3 × 5 × 0.86 to within its own size. It's lit by one light
  from up, left and in front, (−0.457, 0.548, −0.701) towards it, its
  colour near white, about (0.94, 0.96, 0.94): fitted to the logo's
  vertex colours, its own material's ambient the darkest *(inferred)*.
  The light isn't in the scene's list once the frame is drawn.
- The logo's badge, its sprite node `stabadge`, 16 across, upright
  ([formats/spr.md](../formats/spr.md)).
- Along the bottom, a bar, 10 high at y 455 from x 20, (204, 102, 51),
  growing as the files are read: the frames saw it from 7 to 500 long;
  600 when done, the same margin both sides *(guessed)*.

The program looks for both models in `addon\` first, then `SOD\`.

### The movies' colours

Armada's movies, all Bink revision `f`, come out of its `binkw32.dll`
with their chroma's parts exchanged against Bink's usual conversion
(BT.601, studio range): red takes 2.017 × (V − 128), blue 1.596 ×
(U − 128), green −0.813 × (U − 128) − 0.392 × (V − 128), each with
1.164 × (Y − 16) *(inferred)*. So converted, the main menu's panels'
edges meet the background bitmap around them, which they're painted to
match, to within 1.9 to 4.4 of 255 (the screen's 16 bits round by about
2); converted as usual, by 9.7 to 23. The DLL's tables have the usual
constants (at 0x9892 to 0x98a5 in it); why it applies them so, whether
its 16-bit path or how revision `f` keeps its planes, isn't traced.

## In Reluva

- **The game** (`Storm3dEngine`, `src/engines/storm3d/storm3d.cpp`)
  plays the opening movie if the install has it
  (`shell/full_screen_movie.h`), then the main menu
  (`shell/main_menu.h`), its panels' movies played by the clock
  (`shell/panel_movie.h`), with its music and sounds. Quit leaves, as
  Esc does; Replay Intro plays the movie again; Single Player opens the
  single-player screen (`shell/single_player.h`), its tooltips and its
  mission panel, the campaigns and missions open as the player's
  progress (`shell.set`) says;
  O.K. there shows the loading screen (`shell/loading_screen.h`), which
  reads the mission's map and its objects' definitions and models, its
  bar growing, and comes back to the races, as the mission doesn't run
  yet; Esc gives up sooner. Options opens the Options screen
  (`shell/options_screen.h`), and from it the settings screens
  (`shell/settings_screen.h`). Their switches and sliders change the
  player's preferences, which the game reads as it starts and writes as
  it ends. The volumes set the mixer's; the rest change nothing yet.
  The lists show the one mode Reluva draws and its driver, "Reluva", and
  don't open. The player's files are in Reluva's save folder for the
  game, read before the install's (`player_files.h`).
  Multiplayer opens the Multiplayer Connection screen
  (`shell/multiplayer_screen.h`), where a name can be typed; its ways
  to play do nothing yet but Instant Action, which opens the Game Setup
  screen (`shell/game_setup_screen.h`). There, clicking a list turns it
  to its next value *(guessed)*, as the frames never show one open, and
  LAUNCH starts a skirmish on its map ([play.md](play.md)). Load Game opens the Load Game screen (`shell/load_game_screen.h`),
  its list empty. Credits shows the credits (`shell/credits_screen.h`),
  back to the menu after the last page or a key or click. The models are drawn as the data viewer's are
  (`scene/sod_meshes.h`); the logo's specular glint isn't. The version is in Reluva's own
  font: the "System" font isn't in the install.
- **The text** is Arial Bold, as the game asks GDI for it: the machine's
  Arial if it has one, else Liberation Sans Bold, which has Arial's
  measures (the emulator's frames were drawn with it too); a line as
  tall as the font's size when centred, as the emulated GDI has it.
- `reluva-tool storm3d engine-shot <game folder> <out.png> <ms> [-save
  dir] [event...]` runs the game without a window, by a clock of its
  own, the player's moves at set times (`1500:move:90:18`,
  `2000:click:90:18`, `300:key:escape`), its player's files in `dir`,
  and writes the screen at `ms`.
- The movies everywhere (the game, the Movies page, `movie-frame`) are
  converted with the exchanged chroma (`bink::Movie::Rgb::kExchanged`).

## Sources and checks

- **Where the facts come from:** the original's frames from the
  emulator, at rest, with the pointer over a panel and over a label; the
  files it read; its code for the music's flags.
- **How it was checked:** `engine-shot` set beside the original's frames,
  at rest and with the pointer over Single Player: the background, the
  panels, the labels and their looks are where the game has them. The
  single-player screen likewise, with the pointer over the Borg and with
  the Federation's mission panel up: its tooltip, mission name and
  buttons' words fall on the frame's to the pixel. The loading screen
  likewise: its background, logo, badge and bar where the game's are.
  The Options screen is the same as the game's frame in every pixel, and
  so is Sound Settings, at rest and after the same clicks on a switch
  and a slider. Game and Graphics Settings are too, but for 3 pixels of
  anti-aliasing in "Off" and the display driver's name. Given a
  `shell.set` with three Klingon missions open, the mission panel is the
  same as the game's in every pixel.
  The Multiplayer Connection screen, with the pointer over Instant
  Action, and the Load Game screen are the same in every pixel; the Game
  Setup screen, with the pointer over ALL, but for LAUNCH a pixel left
  and six pixels of the stars. The credits' text falls on the
  original's frames in fading in, held and going away, within a few
  hundred pixels; their stars match its frames' to a correlation of
  0.91.
  The unit tests (`storm3d.unit.shell`, `storm3d.unit.credits`) check
  which button and race each place is, the sliders' steps and lengths,
  reading `mshell.set`, and the credits' script, layout and timing.

## Open questions

- Where the program keeps the main menu's places, and the other
  screens' (single player, multiplayer, options, credits).
- What the lists look like open.
- Why the Multiplayer Connection screen stopped answering after Esc
  over the main menu.
- What a saved game holds, and how the Load Game screen lists one.
- Which sounds the settings screens' controls play.
- Which `label.map` entry the Options screen's Previous Menu comes from:
  `escape_menu` has none, and every other screen's says the same.
- What the third bitmap of a label is for, and whether the second movie
  starts over each time the pointer comes back.
- Why the movies' chroma is exchanged.
- What the mission bars' second and third bitmaps are for; whether O.K.
  with no mission chosen does anything (Reluva waits for one
  *(guessed)*); when Omega's panel shows.
