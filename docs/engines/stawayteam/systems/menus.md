# Star Trek: Away Team: the menus

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/main_menu.cpp`, `src/engines/stawayteam/ui/button.cpp`, `src/engines/stawayteam/ui/font.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

The game's menus are screens laid out by `Rules.txt`: each a
`CAnimatedMenuInfo` under `Custom Rules`, `Menu`, with its name and a
definition, an object of the menu's own class that places its buttons
and what's on its screen. The program makes the buttons, names them and
says what each does. So far Reluva has the main menu, its buttons, and
the dialog that asks before quitting.

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
  No (N, Esc) closes it. The other buttons play their sound and lead
  nowhere yet.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`; the pictures and fonts
  in `DATA.dat`.
- **How it was checked:** Reluva run on the retail files, the screen
  looked at: the buttons where `Rules.txt` puts them, their labels in the
  middle, lit and pressed under the mouse, and the dialog's question on
  one line inside its frame, Yes lit under the mouse. Not yet against the
  original running. Unit tests (`stawayteam.unit.menus`) on a font and
  buttons made in the test; the engine test opens and closes the dialog
  and quits through it.

## Open questions

- **Where the dialog goes on the screen.** Reluva puts it in the middle.
- **Esc on the main menu.** Reluva opens the confirm dialog, as Quit.
- **When a button's sound plays:** as it's pressed, or once it's let go
  on. Reluva plays it on the click.
- **The shortcut's line and the label lit:** when the game shows them,
  and how they look.
