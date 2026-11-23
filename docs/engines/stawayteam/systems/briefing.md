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
- **`Briefing Music`**, of `Music/` (`Briefing.ogg` for all).
- **`Script`**, a `CBriefingScript` of `CBriefingScriptLine`s, each
  with `Script` (a script of `Levels/`: `Mission 02/Mission 02` is
  `Levels/Mission 02/Mission 02.RDS`), `Text` (the entry's label),
  `Who is speaking` (the portrait,
  `Cache/Character/Character Portraits/<name>.frm16`) and `Frame to
  start` (-1, but for mission 1's second line, 707).

### The screen

At `Rules.txt`'s places:

- **Its texts:** the definition's static texts ("SPECIAL OPS BRIEFING")
  and two random-number nernies.
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
  made only when the text is taller than the window (`0x47331c`).

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
sound named as its entry (`<Text>.wav`), when the options play
voice-overs.

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
the movie goes on *(inferred)*. Not there yet: the scroll bar (the mouse wheel scrolls instead), the random numbers, the
fade, and a movie's delay. The main menu's song stops while the game's
screens show, and the briefing music plays over and over.

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

- **The voice-overs' files:** the game finds a sound by its name;
  Reluva looks for `<name>.ogg` under `Levels/Voice Overs`. Missions 1
  and 2 have none for their briefings.
- **The briefing music:** where the program plays it isn't traced;
  Reluva plays it over and over while the screen shows.
- **The scroll bar's layout** (`0x4e81c0`).
