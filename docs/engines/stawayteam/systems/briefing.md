# Star Trek: Away Team: the briefing

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/briefing.cpp`, `src/engines/stawayteam/game/team.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

Before a mission that has one (`Show Briefing`, 17 of the 18), the briefing
screen (`CStarTrekBriefingMenu`, made at `0x473900`) plays the
mission's movie and types out its script's lines, a line at a time,
beside the speaker's portrait, each with its voice-over. The player
goes on with CONTINUE or Space, starts again with REPLAY, and leaves
with END BRIEFING for the team selection
([team-selection.md](team-selection.md)), or EXIT for the main menu.
Its layout is `Rules.txt`'s `Briefing Screen` menu; what it shows is
the mission's description.

## How it works

### The mission's briefing

The mission's `.MDS` (a `CMissionDescription`) gives:

- **`Briefing Movie`**, of the install's `DATA/Movies` ("Mission 02/Mission
  02 Final.bik"; 15 missions have `Generic/Starfield.bik`), and **`Loop
  movie`** (1 for 14 of them).
- **`Briefing Music`**, of `Music/` (`Briefing.ogg` for all). It
  is queued as the mission begins, once its starting movie is over,
  with a tenth of a second's fade: the song playing fades out over it
  and then this one starts (as the maps' song,
  [maps.md](maps.md#the-music)), whether or not there's a briefing to show
  (`0x5c3104`; Mission 13 has none, so it plays over its team
  selection); with no other song to follow, it plays again as it ends
  (`0x5bcbeb`), until the maps' Primary Song takes over. The
  debriefing's `Debriefing Music` starts the same way (`0x5c2b6e`).
- **`Script`**, a `CBriefingScript` of `CBriefingScriptLine`s, each
  with `Script` (a script of `Levels/`: `Mission 02/Mission 02` is
  `Levels/Mission 02/Mission 02.RDS`), `Text` (the entry's label),
  `Who is speaking` (the portrait,
  `Cache/Character/Character Portraits/<name>.frm16`) and `Frame to
  start` (-1, but for mission 1's second line, 707).

### The screen

At `Rules.txt`'s places:

- **Its texts:** the definition's static texts ("SPECIAL OPS BRIEFING")
  and two random-number nernies (`CRandomNumberNernie`): rows (`Column
  Height`, 3 and 4) of numbers (`Column Width`, 1 a row) of `Width of
  Numbers` (3) random digits in their `Font` (Numbers Gold, Numbers Gold
  Dark), each the width of so many 0s and `Space Between Columns` apart,
  the rows their font's height and `Space Between Rows` apart
  (`0x54ca80`). Idle for `Idle Time` (5 s), they then change at once and
  every `Time Between Changes` (0.03 s), `Number of Changes` (10) in
  all, and go idle again (`0x54ca10`). The save and load screens have
  two each too.
- **The buttons** (pictures `Briefing/<name>`): EXIT, REPLAY, CONTINUE
  and END BRIEFING. HELP is in `Rules.txt` but the program doesn't make
  it.
- **The title**, *"Mission 02: The Leyte Gulf"* (`"%s %02d: %s"`,
  `0x473d96`), in Placar Blue centred on `Mission Name Pos`; **the
  prompt** in Trek Small centred on `HelpText Pos` (`0x474780`).
- **The movie** (`0x4734f0`) with its top left at `MovieDisplay Pos`,
  shown while it plays (`0x473710`), its sound only when the options
  play voice-overs (`0x47358a`). A `MovieDisplay Delay` other than 0
  would hold it back that long; the retail's is 0.
- **The text window** (`0x473210`) at `TextDisplay Window Pos`, its
  text at `TextDisplay Pos` in it (`0x472a20`), `TextDisplay Size`
  across, in `Briefing Text Font`, `TextDisplay Text Spacing` between
  lines. The speaker's portrait in the middle of `Briefing/Portrait
  Background` at the text's top left, and the lines beside it, the
  background's width and 2 in, until they're past it (its height over a
  line's, and one more) (`0x472f46`).
- **Typing:** a line is typed out at `TextDisplay Text Speed`
  characters a second (25; 90 if `Rules.txt` had none, `0x568d68`), the
  part typed broken into lines as it grows (`0x568e60`).
- **A scroll bar** (`Briefing/Scroll ...` pictures, at `Scroll Pos`) is
  made only when the text is taller than the window (`0x47331c`): a
  window of its own (`0x4727a0`) on `Scroll Background` (19x188) with the
  toolkit's vertical scroll bar (`0x4e81c0`), its arrows `Scroll Up` and
  `Scroll Down` (19x10, Default and Press, drawn stretched to their
  places, `0x4e82a0`) and `Scroll Thumb` (19x4). No retail line is long
  enough to need it.

### Going through the lines

Each frame (`0x474160`):

- **The next line's frame:** when the movie reaches the next line's
  `Frame to start`, the line is shown whole and the next comes.
- **A line typed out:** while there's a next line, the prompt is
  *"Press 'Space' or 'Continue' to continue briefing"*, and the next
  comes by itself once the voice-over has been over for a second and
  the movie, unless it loops, has ended (`0x47435b`). After the last,
  *"Press 'End Briefing' to continue"*, and CONTINUE is disabled.
- **While typing:** *"Press 'Space' to display all text"*, CONTINUE
  enabled.

CONTINUE or Space (`0x474dde`) shows a line being typed whole, or goes
on to the next, with `Interface/Space Bar Briefing.wav`. A line with a
`Frame to start` the movie hasn't reached moves the movie on to it
(`0x4745d6`). Showing a line (`0x472bc0`) starts its voice-over, the
sound named as its entry (`<Text>.wav`, `0x472d1e`), when the options
play voice-overs. It's looked up by that name (`0x56dea0`) in the list
the game makes as it starts of every sound under `Levels`, wherever it
is, an `.ogg` standing for a `.wav` (`0x4f609d`, `0x56db90`); the
retail ones are under `Levels/Voice Overs`, Missions 1 and 2 having
none for their briefings. Sounds under `Sounds/Voice Overs/` and
`Sounds/ENEMIES/` are played only while the options play voice-overs
(`0x4c9140`).

After the lines, items found during the mission before ("Discover Next
Briefing") are described the same way, the movie stopped (`0x474639`).

REPLAY starts the movie, the lines and CONTINUE again (`0x474ba1`). END
BRIEFING marks those items found and leaves (`0x474b2d`). EXIT asks
*"Are you sure you want to quit the game and exit to the main menu?"*,
and yes fades out over half a second to the main menu (`0x474a6d`).

## In Reluva

`BriefingScreen` (`ui/briefing.cpp`), the mission's briefing read by
`loadMission` (`game/team.cpp`). A new game shows the mission's briefing,
then its team selection, if it has one; the team selection's BRIEFING
shows it again. Lines are broken at line breaks as well as spaces
(`Font::wrap`, `ui/font.cpp`): the game's drawing of text of more than
one line breaks it at each line break (`0x4b1670`, looking for it at
`0x4b168c`), each line a font's height and the spacing given under the
one before (`0x4b16ce`). The items found since the last
briefing ([objectives.md](objectives.md#the-score)) are lines after its
own, each said by who found it, and are had from then; while they play
the movie goes on *(inferred)*. The scroll bar's up arrow at its top,
its down arrow at its bottom and the thumb between by how far the text's
scrolled, an arrow a line and the rest of it a window's worth, the text
wrapped short of it *(inferred)*; the mouse wheel scrolls too. The
random numbers are as above, and yes to EXIT fades the screen out over
half a second, the screen's fade times the gamma going evenly from 1 to
0 *(inferred)*. Not there yet: a movie's delay (the retail's is 0).
`MainMenu` queues the briefing music as the mission begins: the song
playing fades out over a tenth of a second, then it plays over and over
until the maps start or the menu comes back.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`; the missions'
  descriptions and scripts in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, missions 1 and
  2, the screen looked at: the movie playing in its frame, the
  portrait, the text typed out beside and under it, Space showing it
  whole, the prompt changing, CONTINUE disabled after the last line, END
  BRIEFING going to the team selection and its BRIEFING back; mission
  1's second line coming with the movie moved on to its frame. Not yet
  against the original running. Unit tests (`stawayteam.unit.menus`)
  read a mission's briefing and go through two lines; `Font::wrap`'s
  breaks.

## Open questions

- **The scroll bar's layout** (`0x4e81c0`).
