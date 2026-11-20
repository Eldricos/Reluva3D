# Star Trek: Birth of the Federation: window layouts (`.wdf`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/window_layout.cpp` · **Checked on:** all 377 `.wdf` of the English retail `stbof.res`, each read and its widgets drawn (October 2026) · **Confidence:** confirmed unless marked

## Overview

377 files in `stbof.res`, one per screen, dialog or panel (`options.wdf`,
`gmesetup.wdf`, `emppick.wdf`, `tactical.wdf` ...). Each is a tree of
widgets: windows holding buttons, menus, sliders, scroll bars, list boxes,
pictures, text boxes and more windows, each with its place, size and
pictures.

## Layout

All values little-endian; `i32` unless said otherwise. Names of files are
13 bytes, zero-terminated, with leftovers after the zero (the editor's
comments note one, in `c_tfship.wdf`, where a name runs into the next
field); "none" means no file.

### File

| Offset | Type | Field |
|---|---|---|
| 0 | `i32` | 2 in every file |
| 4 | `i32` | 1: the type of the first widget, always a window |
| 8 | | the window |

### Widgets

Every widget starts the same way:

| Offset | Type | Field |
|---|---|---|
| 0 | `i32` | id |
| 4 | `i32` | x, from its window's corner |
| 8 | `i32` | y |
| 12 | `i32` | width |
| 16 | `i32` | height |
| 20 | `char[13]` | background picture (a `.tga` in `stbof.res`) |

Inside a window, each widget is preceded by its type (`i32`):

| Type | Widget | After the common part | Confidence |
|---|---|---|---|
| 1 | window | how many widgets (`i32`), how many of each type 1-15 (`i32[15]`), then the widgets, grouped by type in that order (the editor says the game crashes otherwise) | confirmed |
| 2 | button | `i32` -1, `u8` kind (0, 1 or 2), `i32` repeats when held (0, 1), `i32` repeats a second, normal picture, selected picture | layout confirmed; the repeats published |
| 5 | menu | four values (0, 0, then the widget's x and y), two fonts, the menu (`char[13]`, a `.mnu`: see [mnu.md](mnu.md)), left, top, right and bottom pictures, one more file name | confirmed |
| 7 | slider | `i16` 0, maximum, how many cells, the bar's width (the widget's), `i32` 0 or the maximum again, cell off, cell on, marker off and marker on pictures, the bar's height (about the widget's), `i32` 0, `i32` 0. UE reads the last three as the maximum, cells and width again, but they aren't (an intelligence slider: 310, 31 cells, 186 wide; then 22, 0, 0) | layout confirmed; maximum and cells published |
| 8 | scroll bar | `i32` 0 or 1, handle on and handle off pictures, minimum, maximum | layout confirmed; minimum and maximum published |
| 12 | list box | `i32` -1, two fonts, 10 column widths, how many columns (1-10) | layout confirmed; column widths and count published |
| 13 | picture | `i32` -1 | confirmed |
| 14 | text box | text rectangle (x, y, width, height), a font | layout confirmed; the text rectangle published |

In 93 of the 377 files more widgets follow the last one the counts cover
(in `c_ship.wdf`, a whole text box): leftovers, like those after the
names. They're not read.

Windows hold windows, so a layout is a tree. A widget's place on the screen
is its own x and y plus those of every window it's in, and the editor
draws each picture cut to its widget's rectangle.

### Fonts

A font (48 bytes):

| Offset | Type | Field | Confidence |
|---|---|---|---|
| 0 | `u8[3]` | colour: red, blue, green (in that order, as the editor reads it): the options screen's names show in it (below) | confirmed |
| 3 | `i32[7]` | flags, all 0 or a single 1: [0] right-aligned, [1] centred, neither: left-aligned; [2] and [3] not known | confirmed |
| 31 | `i32` | 1: use this font, 0: the game's default | published |
| 35 | `char[13]` | the font (a `.fnt` in `stbof.res`, see [fnt.md](fnt.md)) | confirmed |

Most fonts (1,690 of 1,760) say 0, "use the game's default", with colour
0; the 70 that don't are in the options and game setup screens, the
save/load list, and each race's system, labour, tech and tactical screens.
The fonts named are the race fonts (`cardsn`, `ferngi`, `human`, `klingn`,
`romuln`) at 9, 15, 20 and 28.

The first two flags are the text's alignment: found by comparing the game
setup screen in the recording with `gmesetup.wdf`, whose right-aligned
galaxy values have the first, its centred preferences the second, and its
left-aligned civilization levels neither.

### Menus

A menu widget names a `.mnu`, the items of its pop-up menu. Their layout,
and what the menu widget's own values are, is on [mnu.md](mnu.md).

## Behaviour

### Pictures and transparency

The pictures are 16-bit TGAs. What isn't to be drawn is magenta (red and
blue full, green 0), which shows as large pink areas if drawn as it is. The
game's renderer skips a colour key: `trek.exe` imports MPR's
`MPRSetChromaKey` *(from the code)*, set by the executable rather than
stored in the files *(inferred)*.

## In Reluva

`WindowLayout` (`src/engines/botf/formats/window_layout.cpp`) reads each
layout's widget tree. reluva-viewer's Birth of the Federation Layouts page
draws each one, its widgets drawn with their pictures and outlined by type.
Magenta is drawn as transparent (`applyColorKey`, in
`src/engines/botf/ui/assets.cpp`); the Images page shows the pictures both
ways (K toggles the colour key). The unit tests (`botf.unit.layouts`:
"window layouts" and "colour key") cover it.

## Sources and checks

- **Where the facts come from:** the Ultimate Editor's reader and window
  editor (Java, GPL-2.0, <https://gitlab.com/stbotf/ultimate-editor>,
  commit `7ee45c4b`, files `ue/edit/res/stbof/files/wdf/` and
  `ue/gui/stbof/wdf/`), read for the layout only, October 2026; see
  [tables.md](tables.md#other-sources). The alignment flags from a
  recording of the game setup screen; the colour's byte order from a
  screenshot of the options screen, November 2026.
- **How it was checked:** against every `.wdf` of the English `stbof.res`
  (`tools/botf-re/botf_res_sample.py --ext wdf mnu --all-whole`), October
  2026: all 377 read, their widgets drawn where the game shows them (by
  eye, on the viewer's Layouts page).

## Open questions

- What the button kinds are. (Kind 2 is on the empire selection screen's
  name buttons, which stay down once clicked, one at a time; kind 0 on push
  buttons like Accept and Cancel.)
- What the fonts' third and fourth flags and the 0s and -1s mean.
- How the game finds a widget: presumably by its id, from the executable
  *(guessed)*.
- Which text the colour applies to. On the options screen the names show
  in it (its light blue, stored `19 fa be`, is (25, 190, 250) only in this
  order), but the values beside them, with the same font, don't; on the
  game setup screen the timers' `00 ff 00` shows as pale green with some
  red. The executable presumably colours the values itself *(inferred)*
  (see [screens.md](../systems/screens.md)).
