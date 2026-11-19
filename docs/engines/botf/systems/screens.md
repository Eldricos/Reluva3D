# Star Trek: Birth of the Federation: the screens

> **Status:** partial · **Code:** `src/engines/botf/ui/` · **Checked against:** a recording of the game · **Confidence:** confirmed unless marked

## Overview

The screens rebuilt so far: the main menu, and those that start a new
game, the game setup ("Configuration") screen and the empire selection
screen. Each is put
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
  them at 35% of their brightness *(inferred)*: the recording doesn't show
  one button both ways, but the figure is the one that, with the panels'
  dimming below, matches every button the recording shows.
- **Behind a panel** (game setup, empire selection) every button is dimmed
  further: in the recording the six buttons that could be used are about
  28% as bright as on the menu, and the three that couldn't 80% of their
  own. Reluva draws them all at 35% of 80%, 28% *(inferred)*.
- **The version:** the recording's menu shows "1.0.2" at its bottom left,
  which isn't in the layout: the executable draws it.

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
- `ui/main_menu.cpp`, `ui/setup_screen.cpp` and `ui/empire_screen.cpp`:
  each screen's controls.
- `botf.cpp` runs them as the game does, from the main menu. There's no
  game to start yet, so the empire screen's Accept comes back to the main
  menu, and the menu's other buttons do nothing yet
  ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
- A button shows its "selected" picture while it's held *(guessed)*; the
  empire screen's Accept does nothing until an empire is chosen
  *(guessed)*; clicking an emblem chooses its empire *(guessed)*.

## Sources and checks

- **Where the facts come from:** `gmesetup.wdf`, `emppick.wdf` and
  `opening.wdf`; the manual's chapters 4 ("Main Menu") and 5 ("Single
  Player Game", First Edition, May 1999) for each control's job, choices
  and defaults; a recording of the game ("Let's Play Star Trek Birth of the
  Federation, UFP, p1": the main menu at 0:00-0:57, game setup at
  0:57-1:17, empire selection at 1:16-1:45), October 2026. The buttons'
  brightness was measured on the recording's frames, the menu's and the
  setup screen's, against parts of the screen that are the same in both.
- **How it was checked:** the screens' text, drawn by Reluva with the
  English `lexicon.dic`, laid over stills of the recording (scaled from its
  960x720 to 800x600), every value landing on the game's own; the
  description's line breaks worked out from the font's widths against the
  recording's. `botf.unit.screens` checks the controls' choices and the
  menu's buttons, and `botf.engine` goes from the main menu through both
  screens and back.

## Open questions

- The timers' choices beyond None.
- Whether the game shows a held button's "selected" picture on the setup
  and empire screens, as the main menu does under the pointer.
- How the game dims a button that can't be used, and the menu behind a
  panel: the 35% and 80% are inferred from the recording, not the game's
  own pictures. Settled by comparing the `mc*1` and `mc*2` pictures with
  the recording.
- When Continue can be used: with a saved game, or only with a game in
  progress.
- What `es_?icn1`, `es_?0` and `es_?1` are for, and whether pointing at an
  emblem changes it.
- How the game picks its text colours.
