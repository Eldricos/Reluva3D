# Star Trek: Armada: the front end's text (`label.map`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/label_map.cpp` · **Checked on:** the English retail 1.0 install's `label.map` · **Confidence:** layout confirmed; meanings inferred

## Overview

`label.map`, in the install's folder, holds the front end's words: the
screens' titles and buttons, its messages, and the missions' names. The
program reads it as its main menu opens
([research/startup-trace.md](../research/startup-trace.md#the-files-it-opens-in-order)).
It has 26 sections and 425 entries, one for each screen or popup:
`main_menu`, `single_player`, `mission_select`, `multiplayer_setup`,
`escape_menu`, `race_names`...

## Layout

Text, in Windows-1252 (one character above 127, a bullet), lines ending
with CR LF:

```text
single_player {
	+ previous_menu		{PREVIOUS MENU}
	+ federation_incomplete	{Federation campaign not complete}
}
```

- A line starting `#` is a comment; the file opens with a banner of them.
- A section: its name, then `{` at the line's end; `}` alone closes it.
- An entry: `+`, its key, then its text between braces, tabs or spaces
  between. A key may hold a dot (`fed1.bzn`): `mission_select` names
  each mission by its map.
- Some texts begin `<System>`: by their place, a message's sender
  *(guessed)*.

Several screens draw their words from bitmaps instead (the main menu's
labels, the single-player screen's title and buttons), so some entries
go unused *(inferred)*: `main_menu`'s `button_font` names Arial.

## In Reluva

`LabelMap` (`src/engines/storm3d/formats/label_map.h`) reads it, each
entry's text as UTF-8, and refuses an entry outside a section, a
section left open, or a line that's neither. The single-player screen
takes its tooltips, its mission panel's buttons and the missions' names
from it ([systems/front-end.md](../systems/front-end.md)).

## Sources and checks

- **Where the facts come from:** the file itself; the game's frames,
  which show its texts where the screens draw them.
- **How it was checked:** Reluva's game reads the install's whole; the unit
  tests (`storm3d.unit.label_map`) read the layout written as the game's
  is, and the broken forms.

## Open questions

- Whether the program reads the keys of every section, and what the
  unused ones were for.
