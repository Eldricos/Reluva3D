# Text files and script commands

`Text/` holds the game's tables and dialogue. `Text/Dialogue/Events`
holds one small file per dialogue event, and `Text/Movies` the
cutscenes' subtitles.

## Format

Worked out from the files themselves (`acts.txt`, `itemlist.txt`,
`combinelist.txt`, `fonts.txt`, `interface.txt`, `navigation.txt`,
`uselist.txt` and an event file):

- UTF-16 little-endian with a byte order mark (`ff fe`), CRLF line ends.
- Tables are tab-separated, with the first row as headings. A cell
  containing a comma is in double quotes, with quotes inside doubled. This
  is what a spreadsheet saves as "Unicode text". Trailing rows of nothing
  but tabs are padding.
- `0` in a cell means none.

`reluva-tool awe-table <file>` prints a table row by row, with each
cell's commands split out.

## The tables

| File | Columns |
|------|---------|
| `acts.txt` | Act, PreLoad, Location to Load, Post Load, Time of Day, Weather, Train Moving, Completion Conditions, Event, Music, What to do. One row per act (01A...09A); the script cells are run around loading the act |
| `itemlist.txt` | Item Name, In-game Item Description, Translation, Examine Event, Examine Description, Translation, Flipped Description, Translation |
| `combinelist.txt` | Parts to Combine, Result, Can Decombine, Can Combine, Condition, Event |
| `uselist.txt` | Use this Item, On this character, Condition, Event |
| `navigation.txt` | Tag, Text, Translation, Additional Text: exits' labels (`To Salon Car`) |
| `interface.txt` | Tag, Text, Translation: interface strings (`quit2`: "Quit program?") |
| `fonts.txt` | Font Name, Size, Desc, Effect, Uses; then rows of ARGB colours for text kinds. Calisto MT, Arial and Arial Narrow are in `Interface/Fonts`; Times New Roman and Courier New (for the newspaper clippings and the fingerprints) aren't shipped. Reluva takes sizes as Windows points at 96 DPI (12 points: an em of 16 pixels) and stands in Arial for the missing fonts |
| `Movies/<movie>.txt` | subtitles, in the dialogue tables' columns (Name, Act, Conver., Thread, Nest, Speaker, Target, Line, Line Translation, Condition, Repeat, Prevent, Event, Povent, Duration, Optional, Notes). Duration is "start, length" in seconds from the movie's start |

The "Translation" columns are all `0` in the English release.

## Script commands

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

Verbs seen in the data so far: `act`, `add`, `anim`, `anima`, `attach`,
`fade`, `has`, `look`, `movie`, `rotate`, `scene`, `setchar`, `sound`,
`sounda`, `tag`, `take`, `talk`, `turn`, `wait`, `walk`, `yesno`. The
executable's UTF-16 strings also include `camera`, `change`, `clear`,
`combine`, `detach`, `gotoscene`, `globalhide`, `globalshow`, `hide`,
`hideshadow`, `letterbox`, `music`, `pause`, `play`, `puzzle`,
`removestate`, `removetag`, `reset`, `restore`, `resume`, `run`, `save`,
`savepos`, `script`, `show`, `showshadow`, `stop`, `timer`, `walka`,
`walkfast`, `wear`, `weather`. A list like this is only a lead: the full
set, with each verb's arguments, comes from the script interpreter in
`motoe.exe`, still to be read.
