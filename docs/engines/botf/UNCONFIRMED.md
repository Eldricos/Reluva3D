# Star Trek: Birth of the Federation: unconfirmed items

What Reluva does that hasn't been checked against the running original.
Each page marks the rest of what it hasn't confirmed; these are the items
that change how Reluva behaves.

## Read from the code, not seen running

None: nothing of the game's code has been read.

## Seen running, not traced in the code

- **The screens' text colours** are measured from a recording, as the
  files don't hold them ([systems/screens.md](systems/screens.md)).
  Settled by: the game's own pixels, from screenshots of the running game.

## Inferred

- **The speech and music's sample rate,** 22,050 Hz, isn't stored: it's
  the rate the sound effects use, and the one they sound right at
  ([formats/snd.md](formats/snd.md)).
- **The button under the pointer is drawn with its second picture:** the
  recording shows it lighter, though elsewhere it shows the pointer inside
  Quit Game's rectangle and the button not lit
  ([systems/screens.md](systems/screens.md)). Settled by: pointing at each
  button's edges in the running game.
- **The options screen:** which text box shows which word, a volume's
  click lighting the cells up to the mouse, `volbarof.tga` as the unlit
  bar, and the settings the game setup screen shares with it kept as one
  set ([systems/screens.md](systems/screens.md)). Settled by: a recording
  or screenshots of the screen.

- **Load Game's slots:** slot *n* is `game`*n*`.sav`, the left field its
  name and the right its description ([systems/screens.md](systems/screens.md),
  [formats/sav.md](formats/sav.md)). Settled by: a screenshot with saves in
  it.

## Guessed

- **On Load Game:** empty slots can't be chosen, Accept does nothing until
  one is, and the text's colours ([systems/screens.md](systems/screens.md)).
  Settled by: the running game.

- **On the options screen:** 3D acceleration's choices (Reluva shows YES
  or NO) and default, the volumes starting at the maximum, Random Events
  shown as ON or OFF as on the setup screen, the text in its fonts' own
  colours, and the screen drawn over the dimmed main menu
  ([systems/screens.md](systems/screens.md)). Settled by: the running
  game.

- **On the screens:** a button shows its "selected" picture while it's
  held; the empire screen's Accept does nothing until an empire is
  chosen; clicking an emblem chooses its empire; the emblems drawn are
  `es_?icn0`, not `es_?icn1` ([systems/screens.md](systems/screens.md)).
  Settled by: watching them in the running game.
- **The colour key is magenta** (31, 0, 31 in 15-bit colour): the
  executable sets it, and magenta is what the pictures leave undrawn
  ([formats/wdf.md](formats/wdf.md)). Settled by: no picture showing pink
  where the game shows none.

## Settled

- **November 2026:** how dim the main menu draws buttons that can't be
  used (35%) and every button behind a panel (28%), measured on the
  recording against the `mc*.tga` pictures
  ([systems/screens.md](systems/screens.md)).
- **November 2026:** the main menu dimmed behind the game setup and empire
  selection screens, from the recording.
- **November 2026:** the text boxes' alignment (their fonts' first two
  flags) and the game setup screen's galaxy pictures, from a recording
  ([systems/screens.md](systems/screens.md)).
