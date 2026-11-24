# Star Trek: Armada: the front end

> **Status:** partial · **Code:** `src/engines/storm3d/shell/` · **Checked against:** the original running in Reluva's x86 emulator, its frames, the files it reads and its code · **Confidence:** seen, not traced unless marked

## Overview

Before a game the program shows its front end: the opening movie, then
the main menu, and from it the single-player screens, multiplayer's
setup, the options, the credits. Each screen is a modal dialog of the
program's own, which it draws all of itself: a background bitmap, Bink
movies in panels, and labels from bitmaps
([research/startup-trace.md](../research/startup-trace.md#the-files-it-opens-in-order)).
`shell_cfg.h` has buttons' places for some screens, but not the main
menu's as it draws them: those are in the program. The places below were
found by matching the bitmaps and the movies' frames against the
original's frames from the emulator (the host, [hosting.md](hosting.md)).

## How it works

### The opening movie

The program plays `animations\stintro.bik` (looking for it in the
install first, then on the CD's drive, as `setup\animations\stintro.bik`,
where the installer leaves it; [formats/movies.md](../formats/movies.md)),
320 × 240 shown over the whole 640 × 480 screen, doubled. Esc ends it.

### The main menu

Over `bitmaps\main\mainbk.bmp`, four panels each play a movie, 15
frames a second, over and over, and six labels are drawn, each from
three bitmaps (`bitmaps\ms\<name>1.bmp` to `3`), magenta (255, 0, 255)
left out:

| Button | Label, at | Panel's movie, at |
|---|---|---|
| Single Player | `MSsingl` (22, 10) | `Single01.bik`, 226 × 180, (0, 45) |
| Multiplayer | `MSmulti` (488, 10) | `Multi01.bik`, 226 × 174, (353, 32) |
| Options | `MSoption` (22, 452) | `Const01.bik`, 370 × 160, (0, 285) |
| Credits | `MScredit` (513, 433) | `Credit01.bik`, 200 × 200, (408, 217) |
| Quit | `MSquit` (544, 415) | none |
| Replay Intro | `MSintro` (439, 451) | none |

The labels' places fit the frame to within 2.5 of 255 a pixel; the
panels' place their movies' frames on it. A button is its label and its
panel: with the pointer over the construction yard the game lit
Options. The label under the pointer is drawn from its second bitmap;
the third, by the order, is for it pressed *(guessed)*. The panel under
the pointer plays its second movie (`Single02.bik`...), its frames
matching it, 0.88 against 0.84 for the first *(inferred)*.

In the bottom right the program writes its version, "v1.0", red (181, 0,
0 as shown), from (605, 467), in GDI's "System" font, bold
(`DrawTextExA`).

Its sounds: as it opens, `sounds\effects\shellsd0.wav`, three minutes of
music, played over and over, `PlaySoundA`'s flags 0x201f having
`SND_LOOP` (0x546195) *(from the code)*; as the pointer comes onto
a button, `cornron.wav`; a click, `cornrclk.wav`.

### The single-player screen

Single Player opens it over `bitmaps\single\singlebk.bmp`: its title
(`bitmaps\sp\SPtitle.bmp`) at (256, 12), Previous Menu (`SPprev1` to `3`)
at (28, 451), Load Game (`SPload`) at (507, 452), and a panel for each
race's campaign, 280 × 190, playing `animations\<race>01.bik` (`02`
under the pointer, its emblem lit): the Federation at (0, 30), the
Klingons at (360, 30), the Romulans at (0, 250), the Borg at (360,
250). Their movies, matched against the frame, sit there to a
correlation of 0.998 or more. `Omega01.bik` (80 × 220), the finale's, is
read but not shown to a new player.

The campaigns open one after another, the races in that order *(inferred:
a new player has only the Federation's)*. The pointer over a race not
open yet writes, centred on its panel, white, in Arial Bold of 16
pixels, which campaign is to finish first: for a new player,
"Federation campaign not complete" over each of the others
(`label.map`'s `single_player` section,
[formats/label-map.md](../formats/label-map.md)).

Clicking an open race opens the mission panel, a dialog the program
moves to (224, 46): `SPpanel.bmp`, its heading (`SPselect`, "Select
Mission") at (243, 75), a bar for each mission open (`SPgame1` to `3`,
the first at (234, 146)), and O.K. and Cancel (`SPok1` to `3`) at (228,
386) and (328, 386). The missions' names and the buttons' words are
text, black, in Arial Bold of 12 pixels, centred on their bars: the
names are `label.map`'s `mission_select` entries for each map, the maps
`mshell.set`'s, six lines a campaign (`none` for none; four missions
each, and the finale's). A new player has the first open: the
Federation's is "Premonitions" (`fed1.bzn`). The races' movies stop
while the panel is up, the one chosen on its lit movie.

### The movies' colours

Armada's movies, all Bink revision `f`, come out of its `binkw32.dll`
with their chroma's parts exchanged against Bink's usual conversion
(BT.601, studio range): red takes 2.017 × (V − 128), blue 1.596 ×
(U − 128), green −0.813 × (U − 128) − 0.392 × (V − 128), each with
1.164 × (Y − 16) *(inferred)*. So converted, the main menu's panels'
edges meet the background bitmap around them, which they're painted to
match, to within 1.9 to 4.4 of 255 (the screen's 16 bits round by about
2); converted as usual, by 9.7 to 23. The DLL's tables have the usual
constants (at 0x9892 to 0x98a5 in it); why it applies them so, whether
its 16-bit path or how revision `f` keeps its planes, isn't traced.

## In Reluva

- **The game** (`Storm3dEngine`, `src/engines/storm3d/storm3d.cpp`)
  plays the opening movie if the install has it
  (`shell/full_screen_movie.h`), then the main menu
  (`shell/main_menu.h`), its panels' movies played by the clock
  (`shell/panel_movie.h`), with its music and sounds. Quit leaves, as
  Esc does; Replay Intro plays the movie again; Single Player opens the
  single-player screen (`shell/single_player.h`), its tooltips and its
  mission panel, for a new player (saved progress comes with saving);
  O.K. there doesn't start the mission yet. Multiplayer, Options,
  Credits and Load Game do nothing yet. The version is in Reluva's own
  font: the "System" font isn't in the install.
- **The text** is Arial Bold, as the game asks GDI for it: the machine's
  Arial if it has one, else Liberation Sans Bold, which has Arial's
  measures (the emulator's frames were drawn with it too); a line as
  tall as the font's size when centred, as the emulated GDI has it.
- `reluva-tool storm3d engine-shot <game folder> <out.png> <ms>
  [event...]` runs the game without a window, by a clock of its own,
  the player's moves at set times (`1500:move:90:18`, `2000:click:90:18`,
  `300:key:escape`), and writes the screen at `ms`.
- The movies everywhere (the game, the Movies page, `movie-frame`) are
  converted with the exchanged chroma (`bink::Movie::Rgb::kExchanged`).

## Sources and checks

- **Where the facts come from:** the original's frames from the
  emulator, at rest, with the pointer over a panel and over a label; the
  files it read; its code for the music's flags.
- **How it was checked:** `engine-shot` set beside the original's frames,
  at rest and with the pointer over Single Player: the background, the
  panels, the labels and their looks are where the game has them. The
  single-player screen likewise, with the pointer over the Borg and with
  the Federation's mission panel up: its tooltip, mission name and
  buttons' words fall on the frame's to the pixel. The unit tests
  (`storm3d.unit.shell`) check which button and race each place is, and
  reading `mshell.set`.

## Open questions

- Where the program keeps the main menu's places, and the other
  screens' (single player, multiplayer, options, credits).
- What the third bitmap of a label is for, and whether the second movie
  starts over each time the pointer comes back.
- Why the movies' chroma is exchanged.
- Where the player's progress is kept, and the spacing of the mission
  panel's bars past the first (Reluva puts them 20 apart *(guessed)*); what
  the bars' second and third bitmaps are for; when Omega's panel shows.
