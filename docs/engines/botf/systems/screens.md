# Star Trek: Birth of the Federation: the screens

> **Status:** partial · **Code:** `src/engines/botf/ui/` · **Checked against:** a recording of the game · **Confidence:** confirmed unless marked

## Overview

The screens rebuilt so far: those that start a new game, the game setup
("Configuration") screen and the empire selection screen. Each is put
back together from its window layout ([formats/wdf.md](../formats/wdf.md)),
the manual (what each control does, its choices and defaults),
`lexicon.dic` ([formats/lexicon.md](../formats/lexicon.md)) for its words,
and a recording of the game for how it looks.

Both are panels drawn over the main menu (`opening.wdf`), whose buttons
stay visible on the right in the recording: the panel's background is
magenta, the colour key, where the menu shows through.

## How it works

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

- `ui/layout_screen.cpp`: what both screens share, a layout drawn over the
  main menu, buttons found under the mouse, text drawn in a text box's font
  and alignment, and wrapped as the empire screen's description is.
- `ui/setup_screen.cpp` and `ui/empire_screen.cpp`: each screen's controls.
- `botf.cpp` runs them as the game does. There's no game to start yet, so
  the empire screen's Accept comes back to the setup, and the setup's
  Cancel, which goes to the main menu in the game, goes back to the
  launcher ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).
- A button shows its "selected" picture while it's held *(guessed)*; the
  empire screen's Accept does nothing until an empire is chosen
  *(guessed)*; clicking an emblem chooses its empire *(guessed)*.

## Sources and checks

- **Where the facts come from:** `gmesetup.wdf`, `emppick.wdf` and
  `opening.wdf`; the manual's chapter 5 ("Single Player Game", First
  Edition, May 1999) for each control's job, choices and defaults; a
  recording of the game ("Let's Play Star Trek Birth of the Federation,
  UFP, p1": game setup at 0:57-1:17, empire selection at 1:16-1:45),
  October 2026.
- **How it was checked:** the screens' text, drawn by Reluva with the
  English `lexicon.dic`, laid over stills of the recording (scaled from its
  960x720 to 800x600), every value landing on the game's own; the
  description's line breaks worked out from the font's widths against the
  recording's. `botf.unit.screens` checks the controls' choices, and
  `botf.engine` clicks through both screens.

## Open questions

- The timers' choices beyond None.
- Whether the game shows a held button's "selected" picture, and what the
  main menu's buttons look like while a panel is open (in the recording
  they look dimmed).
- What `es_?icn1`, `es_?0` and `es_?1` are for, and whether pointing at an
  emblem changes it.
- How the game picks its text colours.
