# Star Trek: Birth of the Federation: the screens

> **Status:** partial · **Code:** `src/engines/botf/ui/` · **Checked against:** a recording of the game · **Confidence:** confirmed unless marked

## Overview

The screens rebuilt so far: the main menu, the options screen, and those
that start a new game, the game setup ("Configuration") screen and the
empire selection screen. Each is put
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

- **Pointing:** the button under the pointer is drawn lighter: its second
  picture, the "selected" one *(inferred)*. In the recording New Single
  Player Game lights as the pointer reaches it, before the click.
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
- **The version:** the recording's menu shows "1.0.2" at its bottom left,
  which isn't in the layout: the executable draws it.

### Options

`options.wdf`, opened by the main menu's Options: a background
(`optbkgnd.tga`), Accept (8100) and Cancel (8101) at the bottom, as on the
other panels. The manual (chapter 13) names what it sets: four volumes, and
"Game Preferences", the settings the game setup screen also has, plus two of
its own. The widgets are matched to them by their order on the screen and
their pictures' names; there's no recording of this screen yet.

| Control | Widgets | What it sets (the manual) | Shows |
|---|---|---|---|
| Title | text box 8122 | | "VOLUME CONTROL" (1416) |
| Voice, Video Playback, Background Music, Sound Effects | sliders 8102-8105, names in text boxes 8123-8126 | each volume, off at the left | "VOICE", "VIDEO", "MUSIC", "SFX" (1413, 1411, 797, 1113) |
| 3D acceleration | button 8107 (`3dacc`), value in 8160 | not in the manual (below) | YES or NO |
| Tactical Combat | button 8106 (`tac_bar`), value in 8161 | Manual (default) or Automatic | MANUAL or AUTOMATIC |
| Random Events | button 8108 (`rdm_bar`), value in 8162 | Yes (default) or No | ON or OFF |
| Autosave | button 8109 (`asave`), value in 8163 | Yes (default) or No | YES or NO |
| Strategic Timer | arrows 8115 (less) and 8114 (more), name in 8116, value in 8117 | as on the game setup screen | "NONE" |
| Tactical Timer | arrows 8119 and 8118, name in 8120, value in 8121 | the same | "NONE" |

- **The settings' names** are in their buttons' pictures (234x24, as
  `3dacc.tga` and `3dacc_on.tga`), not in text; the numbers in brackets are
  the words' `lexicon.dic` entries, found by their text *(inferred: which
  box shows which)*.
- **3D acceleration** isn't in the manual, which has Tool Tips instead;
  the layout has no Tool Tips control. The bar's pictures are `3dacc`, and
  `lexicon.dic`'s entry 0 is "The 3d accelerator options have changed. You
  must exit and restart the program for this change to take effect." What
  it chooses between, and its default, aren't known: Reluva shows YES or
  NO, YES at first *(guessed)*.
- **Shared with the game setup screen:** the manual says Random Events,
  Tactical Combat and the two timers can be set here or there. Reluva keeps
  one set: a new game's setup starts from the options, and the setup's
  Accept writes them back *(inferred)*. The setup screen shows Random
  Events as ON or OFF in the recording, where the manual says Yes and No;
  this screen shows it the same way *(guessed)*.
- **A volume** is 0 to 127 in 41 cells of 7 pixels (the slider's maximum,
  cells and width). The layout names `vol-bar.tga`, the cells lit, for the
  slider; `volbarof.tga` beside it in `stbof.res`, the same size, has them
  unlit, though no layout names it. Reluva draws the lit bar and covers the
  cells past the volume with the unlit one; a click lights the cell under
  the mouse and those left of it, and dragging follows the mouse
  *(inferred)*. The manual: "After you adjust a volume slider, a sound
  sample will play so you can hear the new volume level." The volumes start
  at the maximum *(guessed)*.
- **Accept** keeps the changes and **Cancel** drops them (the manual); both
  go back to the main menu.
- **Colours:** the text boxes' fonts name their own, a pale green for the
  title (145, 200, 145) and a light blue for the rest (25, 190, 250), as the
  reader takes them. Reluva uses those *(guessed)*: on the game setup screen
  the recording's colours aren't the files'.
- **Not used:** four pictures without a picture (8110-8113, 144 wide, 96 or
  128 high) beside the settings' values, perhaps where a list of choices
  opens *(guessed)*; `asavebkg.tga` is 144x96.
- **Behind it:** drawn over the main menu, dimmed, as the other panels are
  *(guessed)*.

### Game setup

`gmesetup.wdf`. The controls, matched to the manual's by their order on
the screen, and checked against the recording:

| Control | Widgets (button, text) | Choices (default first) |
|---|---|---|
| Each empire's civilization level, Cardassian to Romulan | 9223/9224, 9225/9226, 9227/9228, 9229/9230, 9231/9232 | Early, Developed, Expanded, Advanced, Beginning. Shift and a click: every empire takes this one's level, or the next if they all have it |
| Minor races | 9207/9208 | Many, None, Few, Some |
| Difficulty | 9209/9210 | Normal, Hard, Impossible, Simple, Easy |
| Strategic timer | 9211 (less), 9212 (more), 9213 | None; the others aren't known yet |
| Tactical timer | 9214, 9215, 9216 | The same |
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
  Measured from the recording (the brightest pixels of the text): blue (51,
  90, 140) for the preferences and the galaxy, green (120, 180, 150) for
  the civilization levels, pale green (176, 202, 153) for the timers. Most
  of these fonts say "the game's default", so the file doesn't hold them,
  and the timers' own colour (`00 ff 00`) doesn't explain theirs.

### Empire selection

`emppick.wdf`, after the setup screen's Accept.

- **Each empire**, in `race.rst`'s order: a name button (kind 2; its
  pictures, `es_?btn1` and `es_?btn2`, carry the name), an emblem
  (`es_?icn0`, in picture widgets the size of the `es_?icn` pictures), and
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
  The emblems don't change. Accept (9110) and Cancel (9111) as on the setup
  screen.
- **The description** of the empire pointed at, as the manual says ("when
  the cursor passes over the empire's icon or name"), or else of the one
  chosen, in text box 9112 (`human20.fnt`, left-aligned). It isn't the
  race's description in `racedesc.rst` but `lexicon.dic`'s entries 270-274
  (found by the recording's text: "The Federation is a free society...").
  Its lines are 23 pixels apart (the font's 21 and 2 more), and break where
  a word and a space after it no longer fit the box's 240 pixels: with the
  font's widths, that breaks the Federation's 13 lines exactly where the
  recording does, where wrapping at 240 without the space doesn't (line 8:
  "...have a" / "more favorable...").
- **Not shown in single player:** five small buttons with number boxes
  (9124-9128, 9119-9123; for multiplayer *(guessed)*) and a 250x350
  picture under the description (9113; the `es_?0` and `es_?1` pictures are
  that size). The recording shows none of them.
- **Colours,** measured from the recording: the levels as on the setup
  screen, the description (133, 115, 75).

## In Reluva

- `ui/layout_screen.cpp`: what the screens share, a layout drawn over the
  main menu (dimmed) or as it, buttons found under the mouse, buttons that
  can't be used drawn dimmed and the one under the pointer lit, text drawn
  in a text box's font and alignment, and wrapped as the empire screen's
  description is. The dimmed pictures are made by `ui/assets.cpp`.
- `ui/main_menu.cpp`, `ui/options_screen.cpp`, `ui/setup_screen.cpp` and
  `ui/empire_screen.cpp`: each screen's controls.
- `botf.cpp` runs them as the game does, from the main menu, and keeps
  the options (while it runs: they aren't saved yet). There's no game to
  start yet, so the empire screen's Accept comes back to the main menu, and
  the menu's other buttons do nothing yet
  ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
- A button shows its "selected" picture while it's held *(guessed)*; the
  empire screen's Accept does nothing until an empire is chosen
  *(guessed)*; clicking an emblem chooses its empire *(guessed)*.

## Sources and checks

- **Where the facts come from:** `gmesetup.wdf`, `emppick.wdf`,
  `opening.wdf` and `options.wdf`; the manual's chapters 4 ("Main Menu"), 5
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
  screens and back, and through the options, cancelled and accepted. The
  options screen hasn't been seen: it's from its layout and the manual.

## Open questions

- The timers' choices beyond None.
- Whether the game shows a held button's "selected" picture on the setup
  and empire screens, as the main menu does under the pointer.
- Whether the pointer lights a main menu button. In the recording's first
  50 seconds the pointer rests at (625, 483), inside Quit Game's rectangle
  near its left edge, and Quit Game shows its first picture: the pointer's
  hot spot may not be where it's drawn, or only the button's picture, not
  its rectangle, may count.
- The options screen as the game draws it: its colours, 3D acceleration's
  choices, whether a volume's click works as Reluva's does, and the
  pictures 8110-8113. Settled by a recording or screenshots of it.
- Where the game keeps the options between runs.
- When Continue can be used: with a saved game, or only with a game in
  progress.
- What `es_?icn1`, `es_?0` and `es_?1` are for, and whether pointing at an
  emblem changes it.
- How the game picks its text colours.
