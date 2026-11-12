# Murder on the Orient Express: text files and script commands

> **Status:** decoded · **Code:** `src/engines/awe/tables.cpp` · **Checked on:** the English release: `acts.txt`, `itemlist.txt`, `combinelist.txt`, `fonts.txt`, `interface.txt`, `navigation.txt`, `uselist.txt` and an event file · **Confidence:** confirmed unless marked

## Overview

`Text/` holds the game's tables and dialogue. `Text/Dialogue/Events`
holds one small file per dialogue event, and `Text/Movies` the
cutscenes' subtitles.

## Layout

### Format

Worked out from the files themselves (`acts.txt`, `itemlist.txt`,
`combinelist.txt`, `fonts.txt`, `interface.txt`, `navigation.txt`,
`uselist.txt` and an event file):

- UTF-16 little-endian with a byte order mark (`ff fe`), CRLF line ends.
- Tables are tab-separated, with the first row as headings. A cell
  containing a comma is in double quotes, with quotes inside doubled. This
  is what a spreadsheet saves as "Unicode text". Trailing rows of nothing
  but tabs are padding.
- `0` in a cell means none.

### The tables

| File | Columns |
|------|---------|
| `acts.txt` | Act, PreLoad, Location to Load, Post Load, Time of Day, Weather, Train Moving, Completion Conditions, Event, Music, What to do. One row per act (01A...09A); the script cells are run around loading the act |
| `itemlist.txt` | Item Name, In-game Item Description, Translation, Examine Event, Examine Description, Translation, Flipped Description, Translation |
| `combinelist.txt` | Parts to Combine, Result, Can Decombine, Can Combine, Condition, Event |
| `uselist.txt` | Use this Item, On this character, Condition, Event |
| `navigation.txt` | Tag, Text, Translation, Additional Text: exits' labels (`To Salon Car`) |
| `interface.txt` | Tag, Text, Translation: interface strings (`quit2`: "Quit program?") |
| `fonts.txt` | Font Name, Size, Desc, Effect, Uses; then rows of ARGB colours for text kinds. Calisto MT, Arial and Arial Narrow are in `Interface/Fonts`; Times New Roman and Courier New (for the newspaper clippings and the fingerprints) aren't shipped: the game takes them from Windows. Reluva takes sizes as Windows points at 96 DPI (12 points: an em of 16 pixels), and for those two looks for Windows' own files (`times.ttf`, `timesbd.ttf`, `cour.ttf`, `courbd.ttf`) where Windows, macOS or Linux's core fonts packages put them, then Liberation Serif and Mono, which have the same measurements; Arial only if there's none. A bold style takes the bold face, where it's otherwise drawn twice a pixel apart |
| `Movies/<movie>.txt` | subtitles, in the dialogue tables' columns (Name, Act, Conver., Thread, Nest, Speaker, Target, Line, Line Translation, Condition, Repeat, Prevent, Event, Povent, Duration, Optional, Notes). Duration is "start, length" in seconds from the movie's start |

The "Translation" columns are all `0` in the English release.

### Script commands

Script cells hold commands separated by commas. Event files hold one per
line. Each command is a verb and its arguments, separated by spaces:

```
fade out, movie teaser, movie intro
scene sirkeci.market_01 1 0, fade in, add ticket, add passantoinette
talk dia_observation a+, act 03a
tag act04a >1, tag eavesdrop04a >4
!has coalstiletto, !has printstiletto
sound coalpuff, talk gen_poirotadd 27
look left / wait 1.6 / anima angry1 .2 / anim fanger .02
```

Conditions use the same form (`has`, `!has`, `tag <name> >N`), and
`|` separates alternatives (`scene exterior.forest_01 | scene
exterior.forest_14`).

How the commands run, and every verb the game uses, is in
[../systems/script.md](../systems/script.md).

## In Reluva

`reluva-tool awe table-show <file>` prints a table row by row, with each
cell's commands split out.

## Sources and checks

Worked out from the files themselves: [Format](#format) lists the ones
read.
