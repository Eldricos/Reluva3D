# Star Trek: Birth of the Federation: the screens

> **Status:** partial · **Code:** `src/engines/botf/ui/` · **Checked against:** a recording of the game · **Confidence:** confirmed unless marked

## Overview

The screens rebuilt so far: the main menu and every screen it opens (the
options, Load Game and Save Game, the Hall of Fame, Multiplayer Setup),
and those that start a new game, the game setup
("Configuration") screen and the empire selection screen. Each is put
back together from its window layout ([formats/wdf.md](../formats/wdf.md)),
the manual (what each control does, its choices and defaults),
`lexicon.dic` ([formats/lexicon.md](../formats/lexicon.md)) for its words,
and a recording of the game for how it looks.

The game setup and empire selection screens are panels drawn over the
main menu (`opening.wdf`), whose buttons stay visible on the right in the
recording, dimmed: the panel's background is magenta, the colour key, where
the menu shows through.

## How it works

### Main menu

`opening.wdf`: a background (`mnbkgrnd.tga`) and nine buttons down the
right, each with two pictures. The first screen after the opening movie
(the manual, chapter 4).

| Button | Widget | Pictures | What it does (the manual) |
|---|---|---|---|
| Continue | 139 | `mccont1`, `mccont2` | Loads the last game played |
| New Single Player Game | 140 | `mcnspg1`, `mcnspg2` | The game setup screen, then empire selection |
| New Multiplayer Game | 141 | `mcnmpg1`, `mcnmpg2` | The multiplayer setup screen |
| Load Game | 142 | `mcload1`, `mcload2` | The saved games (and the four tutorials) |
| Save Game | 355 | `mcsave1`, `mcsave2` | Saves the game in progress, in one of 12 slots |
| Options | 356 | `mcoptns1`, `mcoptns2` | The game's options |
| Hall of Fame | 144 | `mcfame1`, `mcfame2` | The ten best games |
| Retire | 9600 | `mcret1`, `mcret2` | The power graph, then the game ends |
| Quit Game | 143 | `mcquit1`, `mcquit2` | Back to Windows |

The widgets are matched to the manual's buttons by their pictures' names
and their order on the screen, as the recording shows them.

- **Pointing:** the button under the pointer pulses: it shows its second
  picture, the "selected" one, at once, then fades to 14% of the way from
  its first to its second over 0.8 seconds and back over 0.8, again and
  again, as the empire selection screen's names do (below). A button
  counts as under the pointer when the pointer's hot spot, its crosshair's
  middle, is in its rectangle: two screenshots, the hot spot on New Single
  Player Game's top row and one row above it, show it lit and not. A
  screenshot of it lit matches Reluva's, 0.3 seconds into the pulse, but
  the pointer.
- **Buttons that can't be used:** at the start, with no game in progress,
  Continue, Save Game and Retire are dimmed and don't light. Reluva draws
  them at 35% of their brightness *(seen, not traced)*: against their own
  `mc*1` pictures, the recording's buttons that can be used come out at
  82-89% (the recording's own levels), these three at 29-33%, 35-39% of
  the others.
- **Behind a panel** (game setup, empire selection) every button is dimmed
  further, those that can't be used no more than the rest: all nine come
  out at 20-26% of their pictures, 24-30% of the menu's own buttons.
  Reluva draws them all at 35% of 80%, 28% *(seen, not traced)*.
- **The version:** "1.0.2" at the bottom left, which isn't in the layout:
  the executable draws it, presumably `Stbof.ini`'s `Version`
  ([game-files.md](game-files.md)), as Reluva does *(inferred: the same
  text)*. It's `human15.fnt`, its glyphs' corner at (20, 511), in the blue
  (24, 101, 189) of the other screens' values: of every font and place
  near, the one that lands on a screenshot's pixels. Without the key,
  Reluva draws none.
- **Checked against a screenshot** of the menu with the pointer on none
  of its buttons: Reluva's matches it pixel for pixel but the pointer,
  buttons and their dimming included.

### Options

`options.wdf`, opened by the main menu's Options: a background
(`optbkgnd.tga`), Accept (8100) and Cancel (8101) at the bottom, as on the
other panels. The manual (chapter 13) names what it sets: four volumes, and
"Game Preferences", the settings the game setup screen also has, plus two of
its own. The widgets were matched to them by their order on the screen,
and checked against a screenshot of the game's screen.

| Control | Widgets | What it sets (the manual) | Shows |
|---|---|---|---|
| Title | text box 8122 | | "VOLUME CONTROL" (1416) |
| Voice, Video Playback, Background Music, Sound Effects | sliders 8102-8105, names in text boxes 8123-8126 | each volume, off at the left | "VOICE", "VIDEO", "MUSIC", "SFX" (1413, 1411, 797, 1113) |
| Tool Tips | button 8107 (`3dacc`), value in 8160 | Yes (default) or No | YES or NO |
| Tactical Combat | button 8106 (`tac_bar`), value in 8161 | Manual (default) or Automatic | MANUAL or AUTOMATIC |
| Random Events | button 8108 (`rdm_bar`), value in 8162 | Yes (default) or No | YES or NO |
| Autosave | button 8109 (`asave`), value in 8163 | Yes (default) or No | YES or NO |
| Strategic Timer | arrows 8115 (less) and 8114 (more), name in 8116, value in 8117 | as on the game setup screen | "None", or the time |
| Tactical Timer | arrows 8119 and 8118, name in 8120, value in 8121 | the same | "None", or the time |

- **The settings' names** are in their buttons' pictures (234x24), not in
  text: "TOOLTIPS", "TACTICAL COMBAT OPTIONS", "RANDOM EVENTS",
  "AUTOSAVE". The first bar's pictures are named `3dacc` but show Tool
  Tips, as the manual has it. The numbers in brackets are the words'
  `lexicon.dic` entries, found by their text.
- **Values:** right-aligned in their boxes, as their fonts say. Random
  Events shows YES here, where the game setup screen shows ON. The timers
  show "None" as the entry has it, centred between their arrows, where the
  setup screen shows "NONE".
- **Shared with the game setup screen:** the manual says Random Events,
  Tactical Combat and the two timers can be set here or there. Reluva keeps
  one set: a new game's setup starts from the options, and the setup's
  Accept writes them back *(inferred)*.
- **A volume** is 0 to 127 in 41 cells of 7 pixels (the slider's maximum,
  cells and width). The layout names `vol-bar.tga`, the cells lit, yellow
  to red, for the slider; `volbarof.tga`, the same size, has them unlit
  (dark purple), though no layout names it. In the screenshot the cells
  are lit from the left and unlit past the volume, from the slider's own x
  (the third pixel of each 7 lit), as Reluva draws them. A click lights the
  cell under the mouse and those left of it, and dragging follows the
  mouse *(guessed)*. The manual: "After you adjust a volume slider, a sound
  sample will play so you can hear the new volume level."
- **The volumes and settings at first** are `Stbof.ini`'s
  ([game-files.md](game-files.md)): after a fresh install voice, music and
  sound effects are 100 and video 127. 100 shows 33 cells of 41, so the
  lit cells are the volume's share of 41 rounded up *(inferred)*. Without
  the file, Reluva starts there and with the manual's settings.
- **Accept** keeps the changes and **Cancel** drops them (the manual); both
  go back to the main menu.
- **Colours:** the title and the names are in their fonts' own colours, as
  the screenshot has them to the 16-bit screen's precision: (145, 200,
  145) shows as (140, 199, 140), (25, 190, 250) as (24, 186, 247). The
  settings' values (24, 101, 189) and the timers' (173, 227, 115) aren't
  their fonts': the executable's own, measured from the screenshot.
- **Each setting's list of choices:** clicking a setting's bar opens a
  list beside it, in the empty picture on its row (8111 Tool Tips, 8110
  Tactical Combat, 8112 Random Events, 8113 Autosave), over the values;
  the bar shows its lit picture meanwhile. The list is a background with a
  bar along its top (`asavebkg.tga`, 144x96; `tactbkg.tga`, 144x128, the
  same bar, for Tactical Combat by its name *(guessed)*) and the choices'
  buttons, 124x28, from (20, 36) in it and 32 apart: `yes_on.tga` /
  `yestxtop.tga`, `no_on.tga` / `notxtop.tga`, and for Tactical Combat
  `auto_on.tga` / `autotxto.tga` first, then `man_on.tga` / `man-txt.tga`,
  in that order whichever is set (the user, in the game), the choice set
  lit. Screenshots of Tool Tips' and Tactical Combat's
  lists open match Reluva's pixel for pixel but the pointer (and a value
  peeking through). A choice is set at a click and the list closes; a
  click anywhere else, the setting's bar too, only closes it, changing
  nothing (the user, in the game).
- **Behind it:** drawn over the main menu, dimmed, as the other panels
  are. The bar along the top reads "OPTIONS" where the menu's has none;
  that isn't in the layout's text boxes, so presumably it's in
  `optbkgnd.tga` *(guessed)*.

### Hall of Fame

`fame.wdf`, opened by the main menu's Hall of Fame: a screen of its own,
not drawn over the menu, with its background (`h_bk.tga`, "HALL OF FAME"
along the top, "NAME:", "DESCRIPTION:" and "SCORE:" over the list) and
Return (9950) at the bottom right. The manual: "lists the top 10 games.
The name of the winning empire, the description of the victory and the
winning score are listed."

- **The rows:** ten empty picture widgets (9951-9960, 768x36, 38 pixels
  apart from y 171) where a row goes. Each row is framed with its empire's
  picture, `h?_fm1.tga` with the empire's letter (Cardassians `c`,
  Federation `h`, Ferengi `f`, Klingons `k`, Romulans `r`): each of the
  screenshot's rows matches its empire's frame and no other. On it,
  `fameitem.wdf`'s three text boxes, from the row's corner: the name (9962,
  at 52, 10), the description (9963, at 296, 10) and the score (9964, at
  598, 10), all `human15.fnt`, left-aligned, in (24, 101, 189), measured.
- **The descriptions** read as the empire, a number and the galaxy:
  "Klingons 381 Large Ring Galaxy". The number is presumably the turn the
  game was won *(guessed)*.
- **After a fresh install** the list holds ten games ("Yobo the Tyrant",
  Klingons, 12865 ... "Tony the Romulan", Romulans, 3253). They aren't in
  any file of the install or the CD (open questions); Reluva's list starts
  empty ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
- **Return** goes back to the main menu; Reluva also takes Return and Esc
  *(guessed)*.
- **Not used:** `hf_bk.tga` (500x200) and the empires' 91x26 buttons
  (`hf_cbtn1.tga` "CARDASSIAN" ...), perhaps for entering a name after a
  game *(guessed)*.

### Load Game

`loadsave.wdf`, opened by the main menu's Load Game: a background
(`ls_bkgd.tga`), Accept (9503) and Cancel (9504) at the bottom, and a list
of twelve slots, the manual's "12 slots available for saved games".

- **The slots** are buttons the width of the list (9507, 9509 ... 9529,
  596x23, 26 pixels apart from y 175), with `ls_ldb1.tga` (a round cap and
  two framed fields) and, lit, `ls_ldb2.tga`. On each, two text boxes in
  `human15.fnt`, left-aligned: on the left (9506, 9508 ... 9528, 158 wide,
  under "NAME:") the slot's file, `game`*n*`.sav`; on the right (9530,
  9532 ... 9552, 370 wide, under "DESCRIPTION:") its description. Slot *n*
  is `game`*n*`.sav`, top down.
- **After a fresh install** the first four slots hold the tutorials,
  `game0.sav` to `game3.sav`, described "Tutorial #1" to "Tutorial #4":
  the games to load while following the tutorial guide in the install's
  folder (the user). The other eight show their empty fields. The description is
  `gameInfo`'s; the archive header's name is the file's
  ([formats/sav.md](../formats/sav.md)).
- **Choosing:** the manual: "click on its slot and the selected game will
  light up. Then click the Accept button to start that saved game." Reluva
  lights the chosen slot with its second picture. Accept is dimmed until a
  slot is chosen (about 38% as bright as Cancel in the screenshot, the main
  menu's dimming). Empty slots are drawn as the others, not dimmed; one
  can't be chosen *(guessed)*.
- **The text** is in the colour of the saved game's empire, the player's
  (`gameInfo`, [formats/sav.md](../formats/sav.md)), in both fields: not
  the fonts' colours (the names' font names (1, 0, 0)). Measured from a
  screenshot of ten saves, the screen being 16-bit:

  | Empire | Colour |
  |---|---|
  | Cardassians | (0, 134, 165) |
  | Federation | (99, 77, 181) |
  | Ferengi | (214, 32, 66) |
  | Klingons | (255, 24, 33) |
  | Romulans | (0, 166, 156), the tutorials' teal: they're Romulan games |

  Drawn by Reluva with the same ten descriptions and empires, the list
  matches that screenshot pixel for pixel. The user's account: the colour
  follows the empire chosen for the game.
- **The title:** the right end of the bar along the top (9505, 194x36,
  at 386, 102). The layout names `ls_svlb.tga` there, "SAVE GAME"; on this
  screen the game shows `ls_ldlb.tga`, "LOAD GAME", beside it in
  `stbof.res`. "NAME:" and "DESCRIPTION:" are in the background.
- **Shared with Save Game,** below: the same layout (the file's name).

### Save Game

`loadsave.wdf` again, opened by the main menu's Save Game, which can be
used only with a game in progress. The manual: "you have 12 slots
available for saved games. To select a slot, click on the slot in either
the Name Field or the Description Field. In the Description Field, the
galaxy type of the saved game will appear. If you wish to change the text,
click on the Description Field and type in your text. Click the Accept
button to save the game and to resume play. If you save a new game in an
older game's slot, the older game will be erased in favor of the newer
game."

- **As Load Game,** but any slot can be chosen, an empty one too, and the
  title is the layout's own, `ls_svlb.tga`, "SAVE GAME". A screenshot
  before a slot is chosen matches Reluva's pixel for pixel but the
  pointer: Accept dimmed, the tutorials in the first four slots.
- **The chosen slot** lights (its second picture, framed in yellow), and
  its file's name and description turn pink, (239, 182, 198), measured
  from a screenshot. A slot with a saved game keeps its description when
  chosen ("Tutorial #1" stays, seen in the game), to be typed over. An
  empty slot is filled with the game's: its empire, the turn, the galaxy's
  size and shape, and "Galaxy", as "Ferengi 0 Medium Ring Galaxy" (the
  user, in the game; saves so named in the screenshot above), each word
  `lexicon.dic`'s (the empires' plural names). That the number is the turn
  is *(inferred)*: every one seen is 0. Typing changes it, and Backspace
  takes the last character away; only characters the
  field's font has (all in Windows-1252, as `gameInfo` keeps them), up to
  50 and the field's 370 pixels. Reluva takes what's typed whichever field
  was clicked *(guessed)*.
- **The cursor** is a white block, 8 pixels wide and as high as the
  font's glyphs (15), just after the description's last glyph, and
  flashes. In the screenshot, with "You can type here" typed, Reluva's
  matches it pixel for pixel, as does the whole screen but the pointer.
  It's shown 0.3 seconds and hidden 0.3 seconds: a recording of the
  Multiplayer Setup screen's, frame by frame, 9 frames of 30 each time.
- **Loading,** the chosen slot's text is pink too, and Accept lit: a
  screenshot with the first tutorial chosen matches Reluva's pixel for
  pixel but the pointer.
- **Accept** saves into the slot, its old game, if any, replaced; Cancel
  goes back.

### Multiplayer Setup

`mpsetup.wdf`, opened by the main menu's New Multiplayer Game, over the
dimmed main menu: Host (108) and Join (109), kind 2 buttons, and Cancel
(114) down the left; under them three fields, pictures with their names
in them (`m_gmnm0.tga` "GAME NAME:", `m_plynm0.tga` "PLAYER NAME:",
`m_pswrd0.tga` "PASSWORD:"; 702, 703, 701) and a text box in each (112,
113, 700). The manual: Host "determines the connection, sets various game
parameters and names your multiplayer game"; Join joins one as a guest;
"a password, no longer than 13 characters ... This field is optional";
"You can move from field to field by clicking on a new field or pressing
your Tab key." A screenshot of the screen as it opens matches Reluva's
pixel for pixel but the pointer.

- **The panels:** two empty pictures hold the rest. The connection's type
  (`mpconn.wdf`: Network 193, Modem 194, Serial 196) goes beside Host and
  Join (399, 160x131, its size), and the connection's settings to the
  right (200, 303x408): `mpcnet.wdf` (TCP/IP 202 or IPX 203, the host's
  address 207 with its text box 714, Connect 210, and a text box under it
  for the manual's "status connection messages", 211), `mpcmod.wdf` (an
  address book of seven numbers, Connect 491) or `mpcser.wdf` (the baud
  rate, 14,400 to 115,200, 442-446; the port, COM1-COM4, 447-450; Connect
  451). Both places are empty as the screen opens. Choosing Host or Join
  (the same for both) shows the types, and choosing a type its settings,
  the choices lit as kind 2 buttons are; the network starts on TCP/IP.
  A panel is cut to its place: `mpconn.wdf` is 134 pixels high, its place
  131, and the game leaves out the 3 rows past it. Screenshots of each
  panel, and of Join with Serial, 14.4 and COM 1, match Reluva's pixel for
  pixel but the pointer and a few orange specks the game leaves at the
  bottom of the screen (40 pixels, in each).
- **Typing:** a field takes typing once clicked, or with Tab, which goes
  round the three; the host's address once its button is clicked, and an
  address book row once it's clicked. The field typed in shows its
  picture's lit version (`m_plynm1.tga` for `m_plynm0.tga` ...; the host's
  address its button's second picture, an address row nothing) and its
  text in orange (255, 150, 16) with Save Game's cursor; the others' text
  is blue (24, 101, 189). The password shows an X for each character.
  Nothing is typed until a field is clicked (the user, in the game);
  names only as long as their field's width.
- **Serial** starts with no baud rate or port chosen (the user, in the
  game); a click lights one of each.
- **Connect** does nothing: Reluva can't play over a network, modem or
  serial cable ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
  Cancel goes back to the main menu.

### Game setup

`gmesetup.wdf`. It opens with the last game's setup, from `Stbof.ini`
([game-files.md](game-files.md)), or the last one accepted while Reluva
runs; with neither, the manual's defaults below. The controls, matched to
the manual's by their order on the screen, and checked against the
recording:

| Control | Widgets (button, text) | Choices (default first) |
|---|---|---|
| Each empire's civilization level, Cardassian to Romulan | 9223/9224, 9225/9226, 9227/9228, 9229/9230, 9231/9232 | Early, Developed, Expanded, Advanced, Beginning. Shift and a click: every empire takes this one's level, or the next if they all have it |
| Minor races | 9207/9208 | Many, None, Few, Some |
| Difficulty | 9209/9210 | Normal, Hard, Impossible, Simple, Easy |
| Strategic timer | 9211 (less), 9212 (more), 9213 | None, 01:00 to 15:00 a minute apart, 20:00, 25:00, 30:00 to 90:00 ten minutes apart |
| Tactical timer | 9214, 9215, 9216 | None, 00:15 to 05:00 15 seconds apart, 06:00 to 10:00 a minute apart, 15:00 to 30:00 five minutes apart |
| Random events | 9217/9218 | On, Off |
| Tactical combat | 9219/9220 | Manual, Automatic |
| Victory conditions | 9221/9222 | Domination, Vendetta (Team Play is for multiplayer games) |
| Galaxy shape | 9234/9206, picture 9204 (`cc_gsir`, `cc_gsel`, `cc_gsrg`, `cc_gssp`) | Irregular, Elliptical, Ring, Spiral |
| Galaxy size | 9233/9205, picture 9203 (`cc_gzsm`, `cc_gzmd`, `cc_gzlg`) | Medium, Large, Small |
| Accept, Cancel | 9201, 9202 | |

- **Which galaxy picture is which:** the recording shows the shape above,
  the size below.
- **The text:** each value is drawn in its text box's font, aligned as the
  font says (its first flag right, its second centred, neither left: found
  by comparing the recording with the files), in the middle of the box's
  height. Drawn this way over a still of the recording, every value lands
  on the game's own, letter for letter.
- **The words** are `lexicon.dic`'s, but the screen numbers the
  civilization levels ("2 EARLY", where the entry is "EARLY") and shows the
  timers' "None" in capitals.
- **Colours:** the glyphs are shades of grey, and the game colours them.
  Measured from a screenshot: blue (24, 101, 189) for the preferences and
  the galaxy, green (90, 207, 140) for the civilization levels, and yellow
  green (173, 227, 115) for the timers, the blue and the yellow green as
  on the options screen. Most of these fonts say "the game's default", so
  the file doesn't hold them, and the timers' own colour (`00 ff 00`)
  doesn't explain theirs. (The recording's colours had been taken first:
  a video's run lighter and greyer.)
- **Checked against a screenshot,** drawn by Reluva from the game's files
  with the same setup (Ring, Medium, Automatic): every picture and text
  lands on the game's pixels but the two timers' "NONE", a pixel left of
  the game's. The options screen's timers, centred in the same way, match;
  why these don't isn't known. A screenshot with the timers at "01:00" and
  "00:15" shows the same: Reluva's are the game's glyph for glyph, a pixel
  to the left.
- **The timers' choices,** minutes and seconds, as "MM:SS", are the
  user's, stepping through them in the game: 25 for the strategic timer,
  30 for the tactical, None first in both. The arrows step one choice;
  Reluva stops them at either end *(guessed)*.

### Empire selection

`emppick.wdf`, after the setup screen's Accept.

- **Each empire**, in `race.rst`'s order: a name button (kind 2; its
  pictures, `es_?btn1` and `es_?btn2`, carry the name), an emblem
  (`es_?icn1`, in colour, in picture widgets the size of the `es_?icn`
  pictures: a screenshot matches each pixel for pixel, and the recording
  shows them too, before and after a choice; `es_?icn0` are the same in
  blue), and
  its civilization level from the setup screen in the text box under its
  name, numbered as there:

  | Empire | Name | Emblem | Level |
  |---|---|---|---|
  | Cardassians | 9105 | 9100 | 9114 |
  | Federation | 9106 | 9101 | 9115 |
  | Ferengi | 9107 | 9102 | 9116 |
  | Klingons | 9108 | 9103 | 9117 |
  | Romulans | 9109 | 9104 | 9118 |

- **Choosing:** in the recording no empire is chosen at first; the one
  clicked shows its name button's second (yellow) picture and keeps it.
  The emblems don't change. The name under the pointer, if it isn't the
  chosen one, pulses between its two pictures, as the main menu's buttons
  do: measured frame by frame in a recording, the blend jumps to the
  second picture as the pointer arrives, falls evenly to 14% in 0.8
  seconds, rises again in 0.8, and so on; three screenshots fall on that
  curve. Accept (9110) and Cancel (9111) as on the setup screen.
- **The description** of the empire pointed at, as the manual says ("when
  the cursor passes over the empire's icon or name"), or else of the one
  chosen, in text box 9112 (`human20.fnt`, left-aligned). It isn't the
  race's description in `racedesc.rst` but `lexicon.dic`'s entries 270-274
  (found by the recording's text: "The Federation is a free society...").
  Its lines are 23 pixels apart (the font's 21 and 2 more), the first
  glyphs a pixel below the box's top, as all text is drawn (In Reluva,
  below). A word goes on a line when it and the space after it fit the
  box's 240 pixels; a paragraph's last word, with no space after it, needs
  only its own width. With the font's advances as the game uses them
  ([formats/fnt.md](../formats/fnt.md)), that breaks the Federation's 13
  lines where the recording does (line 8: "...have a" / "more
  favorable..."), and the Klingons' 12 where a screenshot does: line 7
  "...diplomacy, but" (with "they" 237 pixels, and its space past 240),
  and the last "suited to predatory group attacks." (239).
- **Not shown in single player:** five small buttons with number boxes
  (9124-9128, 9119-9123; for multiplayer *(guessed)*) and a 250x350
  picture under the description (9113; the `es_?0` and `es_?1` pictures are
  that size). The recording shows none of them.
- **Colours,** measured from a screenshot: the levels (24, 101, 189), as
  the options' values, and the description (206, 117, 66).

## In Reluva

- `ui/layout_screen.cpp`: what the screens share, a layout drawn over the
  main menu (dimmed) or as it, buttons found under the mouse, buttons that
  can't be used drawn dimmed and the one under the pointer lit, text drawn
  in a text box's font and alignment, and wrapped as the empire screen's
  description is. A line of text is placed as the game places it: its
  glyphs a pixel below the line's top (the cells' frame row above them),
  a single line's top centred in the box, `(height - line height) / 2`,
  measured on every screenshot's text. The dimmed pictures are made by `ui/assets.cpp`.
- `ui/main_menu.cpp`, `ui/options_screen.cpp`, `ui/load_save_screen.cpp`,
  `ui/fame_screen.cpp`, `ui/multiplayer_screen.cpp`, `ui/setup_screen.cpp`
  and `ui/empire_screen.cpp`: each screen's controls. A screen can add
  other layouts as panels in its empty pictures (Multiplayer Setup's), and
  typing goes through `ui/layout_screen.cpp` for both screens that take
  it.
- `botf.cpp` runs them as the game does, from the main menu, and keeps
  the options and the last setup, starting from `Stbof.ini`'s and writing
  them back into it ([game-files.md](game-files.md)). Load Game lists the
  install's own saves, not the CD's. There's no game to start or load yet,
  so the empire screen's Accept and Load Game's come back to the main menu,
  and the menu's other buttons do nothing yet
  ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
- A button shows its "selected" picture while it's held *(guessed)*; the
  empire screen's Accept does nothing until an empire is chosen
  *(guessed)*; clicking an emblem chooses its empire *(guessed)*.

## Sources and checks

- **Where the facts come from:** `gmesetup.wdf`, `emppick.wdf`,
  `opening.wdf`, `options.wdf` and `loadsave.wdf`; the manual's chapters 4 ("Main Menu"), 5
  ("Single Player Game") and 13 ("Game Options", First Edition, May 1999)
  for each control's job, choices and defaults; a recording of the game ("Let's Play Star Trek Birth of the
  Federation, UFP, p1": the main menu at 0:00-0:57, game setup at
  0:57-1:17, empire selection at 1:16-1:45), October 2026. The menu
  buttons' brightness was measured on the recording's frames against the
  bottom six rows of each `mc*1` and `mc*2` picture, from the English
  `stbof.res`, November 2026: each picture's best fit, picture 1 for every
  button in every frame.
- **How it was checked:** the screens' text, drawn by Reluva with the
  English `lexicon.dic`, laid over stills of the recording (scaled from its
  960x720 to 800x600), every value landing on the game's own; the
  description's line breaks worked out from the font's widths against the
  recording's. `botf.unit.screens` checks the controls' choices and the
  menu's buttons, and `botf.engine` goes from the main menu through both
  screens and back, through the options, cancelled and accepted, and
  through Load Game with two saves. The options and Load Game screens were
  checked against screenshots of the game after a fresh install (November
  2026): every widget, word, value and colour. Drawn by Reluva from the
  English `stbof.res` with the same games, the Hall of Fame, Load Game and
  the options match the screenshots pixel for pixel but for the pointer,
  the 16-bit screen's rounding, and a few pixels of `8` and `B`.

## Open questions

- Whether the timers' arrows stop at the ends or go round.
- Whether the game shows a held button's "selected" picture on the setup
  and empire screens, as the main menu does under the pointer.
- On the options screen: whether a volume's click works as Reluva's does.
- Why the game setup screen's timers are a pixel right of where their
  text boxes centre them.
- Where the game keeps the options between runs.
- Save Game: whether the number in a new save's description is the turn.
  Settled by saving a game after its first turn.
- Load Game: whether a slot whose file is `Game4.sav` on disk shows that
  or `game4.sav`. Settled by a save in such a slot. (After a fresh install
  that file is 0 bytes, and its slot shows empty: Reluva's reader refuses
  it, so it shows empty too.)
- The Hall of Fame's ten first entries ("Yobo the Tyrant ... 12865" in a
  fresh install's screenshot) aren't in any file of the install or the CD:
  not in `stbof.res` (each of its 6,013 files searched), the saves,
  `Stbof.ini` or the registry. `Trek.icd`, SafeDisc's encrypted
  executable, is the one place left, and it shows no readable text.
- When Continue can be used: with a saved game, or only with a game in
  progress.
- What `es_?icn0`, `es_?0` and `es_?1` are for.
- How the game picks its text colours.
