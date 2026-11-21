# Star Trek: Away Team: the menus

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/main_menu.cpp`, `src/engines/stawayteam/ui/button.cpp`, `src/engines/stawayteam/ui/font.cpp`, `src/engines/stawayteam/ui/credits.cpp`, `src/engines/stawayteam/ui/widgets.cpp`, `src/engines/stawayteam/ui/difficulty.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

The game's menus are screens laid out by `Rules.txt`: each a
`CAnimatedMenuInfo` under `Custom Rules`, `Menu`, with its name and a
definition, an object of the menu's own class that places its buttons
and what's on its screen. The program makes the buttons, names them and
says what each does. Its other windows (the credits, the difficulty,
the settings, the escape menu) are built by a toolkit of the program's
own, which places its widgets itself. So far Reluva has the main menu,
the credits, the difficulty window and the dialog that asks before
quitting.

## How it works

### Text in the game's fonts

A font (`Cache/Fonts/<name>.seq16`, a `CFont`,
[formats/binary-object.md](../formats/binary-object.md)) has a picture
for each character from its `First ASCII` to its `Last ASCII`. The game
draws a line of text a character after another (`0x4b15b4`):

- **Nothing between characters:** each starts where the one before ends,
  so a line's width is the sum of its characters' (`0x4b1440`).
- **Characters it hasn't got** are drawn as its first; those under 20
  (hex 14) are left out (`0x4b15ce`).
- **Capitals only:** a font whose `Last ASCII` is under 95 has its text
  drawn in capitals (`0x4b15bc`). None of the retail fonts is one: all go
  to 255.
- **Fixed width:** each character in the middle of a cell (`0x4b1600`);
  the cell's width isn't traced; Reluva takes the widest character's
  *(inferred)*. None of the menus' fonts is one.
- **Lines** of text that has more than one are a font's height (its
  sequence's bounds) apart, and a spacing its caller gives (`0x4b16ce`).
  The dialog's spacing isn't traced; Reluva puts none.

CFont's first property of its own, which has no name, is the row the
characters stand on: a shortcut's line goes a row under it (`0x5cd191`).
Placar Condensed's is 25, Placar Blue's 9.

### Buttons

A button has a picture for each state, `<name> Default`, `<name> Hover`
and `<name> Press`, from `Cache/Interface/Screens/<menu>/`, and a label
(`0x5ccdc0` sets it, with where it goes, its font and how it's aligned):

- **The label's place:** its top at the button's top and the text
  position's y. Centred across the button (the menus' buttons are, `push
  0x1` at `0x5036fa`), the label starts at half the button's width less
  half the label's, less the text position's x (`0x5cd0d1`); aligned
  right, at the width less both; aligned left, at the x.
- **The shortcut:** an `&` in the label isn't drawn, and the key after it
  is the button's shortcut, its place kept at the button's `+0x55`. A
  line is drawn under it, in a colour that changes with time, when the
  button's flag at `+0x5d` is set (`0x5cd148`), which is likely once the
  player has pressed Alt *(inferred)*.
- **Lit under the mouse:** the label's drawing goes through a
  brightness set from the button's `+0x174`, a number that changes as
  the mouse comes over it, so the label brightens *(inferred)*.

### The main menu

The program names its six buttons, top to bottom (`0x503657`), and what
each opens (`0x503e50`):

| Button | Shortcut | Opens |
|---|---|---|
| START NEW GAME | S | A new game (`0x48be40`) |
| LOAD SAVE GAME | L | The saved games (`0x5017f0`) |
| PLAY NETWORK GAME | N | Network games (`0x5115c0`) |
| SETTINGS | E | The settings, with CONTINUE (`0x4863b0`) |
| CREDITS | C | The credits (`0x5bb410`) |
| QUIT | Q | The confirm dialog, then the game ends (`0x486fd0`) |

Their pictures are `Main Menu/Main Button Default`, `Hover` and `Press`,
334x34, and the font Placar Condensed, named by the program
(`0x50359a`). `Rules.txt`'s `CStarTrekMainMenu` places them
(`0x503d40`): the first's top left at its `Menu Pos` (155,109), each
next under the one before, `Button Spacing` (8) between them; the labels
at `ButtonText Pos` (0,5). A click plays `Interface/Main Screen
Button.wav` (`0x5036b9`), the `.ogg` of that name in `DATA.dat`'s
`Sounds/`, as all the game's sounds are *(inferred)*. What's behind the
buttons is the main menu's screen ([startup.md](startup.md#the-main-menus-screen)).

### The confirm dialog

Quit asks "Are you sure you want to exit the game?" (`0x640058`, through
the program's table of text at `0x503400`), in the dialog `Rules.txt`
calls `Confirm Dialog` (`CStarTrekConfirmDialogMenu`):

- **Its picture:** `Confirm Dialog/Background`, 414x127.
- **The question** (`0x487270`): in Placar Blue, named by the program
  (`0x4872b5`), not `Rules.txt`'s `Menu Font` (Bach White); centred across
  the dialog, its top at `TextCenter Pos`'s y (28), broken into lines no
  wider than the dialog less 128. `TextCenter Pos`'s x (299) isn't used.
- **Yes and No:** at `YesButton Pos` (68,88) and `NoButton Pos`
  (222,88), their pictures `Confirm Dialog/Yes ...` and `No ...` (123x19),
  their labels `&Yes` and `&No` centred, at `Text Pos` (0,4), in Placar
  Blue; a click plays their `Press Sound`, `!None.ogg`, a silence. The
  dialog's `OkButton` is for dialogs that only tell.

### The credits

Credits opens a window over the main menu (`0x5bb410`), which shows
through it:

- **Its picture:** `Escape Menu/Escape Menu Background` (`0x5bb625`),
  414x232, the escape menu's: its middle is black and part see-through
  (alpha 211). It sizes the window.
- **RESUME** (`&RESUME`, `0x642598`), which closes it: a picture button
  ([below](#the-window-toolkit)) on `Escape Menu/Button Background`
  (276x22, `0x5bb4f4`), at 69 across and 27 above the window's bottom
  (`0x5bb674`), its label in Placar Condensed, named by the program
  (`0x5bb4a2`).
- **The credits** roll in the area `Rules.txt`'s `Credits` gives,
  `Credit Pos` (61,21) and `Credit Size` (292x155), in Placar Blue
  (`0x5bb547`), not the `Menu Font` (Bach White).

The roll (`0x4873f0` reads it, `0x487530` draws it):

- **Its lines:** `Credits.txt`'s, up to 999 of up to 255 characters;
  those starting `/` are the file's comments and left out. Blank lines
  are kept.
- **Moving:** the lines are a font's height and 2 apart; the roll moves
  up a line's height every 500 ms (0.002 lines a millisecond), the first
  line coming up from the area's bottom. Once the last has gone up past
  the top by the area's height, it starts again.
- **Fading:** each line's brightness is `sin(pi * top / height)` for its
  top in the area, 0 to 1: dark at the bottom and top, full across the
  middle. The game sets it as a brightness for the drawing (`0x44c4f0`);
  Reluva draws the text that much opaque over what's behind *(inferred)*.
- **Each line**, centred across the area, as the file's comments say:
  - `Role: Name`: with a `:` (the last, if more than one, and not the
    first character), what's before it ends at the middle and what's
    after it starts there.
  - `_Heading`: a heading, with a line under it in dark red (156,0,0).
  - `+Role`: a role, with a line under it in green (0,150,0).
  - Anything else, as it is.

  The line under a heading or role is one pixel high, a row below the
  text's height, as wide as the text and one more (`0x48774d`).

### The window toolkit

The program's windows other than the main menu are made of widgets of
its own toolkit, each placed as it's added to its window (`0x4097f0`,
the placing at `0x40a6d0`):

- **Placing:** a widget is placed by a mode and a distance across and
  down. Of the 23 modes, those the menus so far use: 1, from the window's
  top left; 6, in the middle of the window; 7, under a given widget,
  lined up on its left; 8, under the widget added before (1 if it's the
  first); 13, right of a given widget, lined up on its top. A distance
  given counts in fifths of the font's baseline (Placar Condensed's 25:
  5 pixels each); one left out takes the window's margin (modes 1-4) or
  spacing (most others), both four fifths of its font's baseline, which a
  window can change. Windows then often move widgets to places of their
  own.
- **Picture buttons** (`0x5ba890`): a picture (`Button Background`), the
  label on it in the button's font, from its left at 21 and 1 above its
  top (`0x5eff58`, from the 20 and -2 the button sets at `+0x13c` and
  `+0x138`), a pixel further right and down while it's held down. The
  picture is drawn at a brightness (`0x5ba910`) that starts at 1, goes to
  0.4, and to 1 again while the mouse is over it, 2 a second
  (`0x5efe60`, `0x5f0056`).
- **Toggles** (`0x4e8620`): a box, its widest picture, square, and the
  label after it, at its right and 1 down (`0x4e89c3`); the toggle is as
  wide as both and 1 more, and as high as its highest picture
  (`0x4e8b72`). Drawn (`0x4e6310`): held down on, its `Press` picture;
  checked, its `Press` picture with a cross corner to corner, a pixel in,
  in the toolkit's grey (186,186,186, `0x4084e4`); otherwise its
  `Default` picture. Toggles can be one of a group, one checked at a time
  (`0x4e49b0`).
- **Colours:** the toolkit's widgets have their own, set as each is made
  (`0x408150`): light blue (63,154,255) and others for frames, which the
  menus so far don't draw.
- **Sounds:** the toolkit loads `UI/Button Action.wav`, `UI/Button
  Switch.wav` and `Error.wav` (`0x4082c5`); `DATA.dat` has the first two
  as `Sounds/Button Action.ogg` and `Sounds/Button Switch.ogg`. Which it
  plays when isn't traced.

### The difficulty window

Start New Game opens it (`0x48be40`), over the main menu:

- **Its picture:** `Difficulty Menu/Background`, 414x174; its font
  Placar Blue, named by the program (`0x48be90`).
- **"Select a difficulty level"** (`0x63be90`), centred across it, its
  top at 24 (`0x48c240`).
- **The levels:** a toggle each, Easy and Normal (the program's table at
  `0x63be78`), with `Toggle Default` and `Toggle Press` (15x15): placed
  in the middle (mode 6), then the first 16 above the middle's top less
  the toggle and 4 more, the second 16 above it (`0x48c0c1`); both lined
  up on the left with the widest in the middle across (`0x48c107`). The
  game's level is checked to start with (`0x48c144`).
- **START GAME** (`&START GAME`) in the middle across, 20 above the
  window's bottom (`0x48bf76`), and **CANCEL** (`CA&NCEL`) 4 above it
  (`0x48bfc6`): picture buttons on `Difficulty Menu/Button Background`,
  their labels in Placar Condensed (`0x48bf88`). Opened from a game in
  progress, START GAME is CONTINUE (`0x48bf0b`).
- **START GAME** sets the game's level to the one checked, Easy 1 and
  Normal 2, or leaves it if none is (`0x48c2b5`), and starts the game;
  CANCEL closes the window (`0x48c31a`).

### The settings window (not yet in Reluva)

Settings opens `0x4863b0`, with `&CONTINUE`, worked out so far:
`Configure Menu/Background` (428x277), the window's spacing set to 4
(`0x486406`); CONFIGURE &KEYBOARD and CONFIGURE &SOUND, picture buttons
in Placar Condensed, the first placed by layout 1 a fifth of the
baseline (5 pixels) each way, then moved to 69 across and 36 further
down, to 41 (`0x4864a4`), the second under it 5 apart (layout 8);
Brightness:, Scroll Speed: and Scroll Accel:, labels in Trek Small,
each under the one before, with a slider each (0 to 100, `0x4e4b10`) at
its right, the sliders made as wide as the widest and lined up; &Pause
Game When Hit Off-screen, a check box under them (`0x4e6020`); then
CONTINUE 29 above the window's bottom, RESET &DEFAULTS 4 above it, and
DI&FFICULTY SETTING above that while no game is in progress
(`0x486a01`). How sliders and check boxes are drawn (`0x4e4bd0`, the
toolkit's frames at `0x408000`) isn't worked out yet.

## In Reluva

- **Fonts:** `Font` (`ui/font.cpp`) reads one from `DATA.dat` and draws
  text with it by the rules above.
- **Buttons:** `Button` and `ButtonGroup` (`ui/button.cpp`): the
  pictures for each state, the label and shortcut; lit under the mouse,
  pressed while it's held down on one, chosen when it's let go on the same
  one, or by its shortcut's key (without Ctrl or Alt). The shortcut's line
  and the label's brightening aren't drawn yet.
- **The main menu:** `MainMenu` (`ui/main_menu.cpp`), its screen and its
  buttons as above. Quit, or Esc, opens the confirm dialog, in the middle
  of the screen; Yes (or Y) ends the game and goes back to the launcher,
  No (N, Esc) closes it. Credits opens the credits window (`CreditsWindow`,
  `ui/credits.cpp`, the roll a `CreditsRoll`), in the middle of the
  screen; RESUME, R or Esc closes it. Start New Game opens the difficulty
  window (`DifficultyWindow`, `ui/difficulty.cpp`), in the middle of the
  screen, Normal checked to start with: START GAME (S) keeps the level
  checked and closes it, as the game isn't there to start yet; CANCEL
  (N, Esc) closes it. The other buttons play their sound and lead
  nowhere yet.
- **The toolkit's widgets:** `PictureButton` and `Toggle`
  (`ui/widgets.cpp`), as above, placed where the program's layout puts
  them, worked out by hand for each window. A picture button's click plays
  `Button Action`; a toggle's plays nothing yet.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`; the pictures and fonts
  in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, the screen
  looked at: the buttons where `Rules.txt` puts them, their labels in the
  middle, lit and pressed under the mouse, and the dialog's question on
  one line inside its frame, Yes lit under the mouse; the credits rolling
  and fading inside their window, the headings and roles underlined, the
  main menu showing through; the difficulty window's heading, toggles and
  buttons, a level checked by a click, START GAME lit under the mouse.
  Not yet against the original running. Unit tests
  (`stawayteam.unit.menus`) on a font, buttons, the toolkit's widgets and
  the credits' lines made in the test; the engine test opens and closes
  the difficulty window, the credits and the dialog, and quits through
  it.

## Open questions

- **Where the dialog and the other windows go on the screen.** Reluva
  puts them in the middle.
- **The game's level before one is chosen.** Reluva starts at Normal.
- **Brightness:** whether the toolkit's dims a picture's colours toward
  black. Reluva scales them.
- **The credits' fading:** whether to black or to see-through, and
  whether the lines under headings fade too. Reluva fades the text to
  see-through and draws the lines whole.
- **Esc in the credits.** Reluva closes them.
- **Esc on the main menu.** Reluva opens the confirm dialog, as Quit.
- **When a button's sound plays:** as it's pressed, or once it's let go
  on. Reluva plays it on the click.
- **The shortcut's line and the label lit:** when the game shows them,
  and how they look.
