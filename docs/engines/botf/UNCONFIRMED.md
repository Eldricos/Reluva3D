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
- **The options screen:** the settings the game setup screen shares with
  it kept as one set ([systems/screens.md](systems/screens.md)). Settled
  by: a setting changed on one screen seen on the other.
- **A saved game's LZSS window** starts full of spaces for its text files,
  from the Ultimate Editor alone ([formats/sav.md](formats/sav.md)).
  Settled by: `result.lst` decoded both ways once its pieces are read.

## Guessed

- **On Save Game:** an empty slot's description when chosen (the galaxy's
  shape),
  typing whichever field was clicked, the cursor's flashing every half a
  second, and on Load Game the chosen slot's text pink too
  ([systems/screens.md](systems/screens.md)). Settled by: a screenshot of
  an empty slot just chosen, a recording, and a slot chosen on Load Game.

- **On Load Game:** empty slots can't be chosen
  ([systems/screens.md](systems/screens.md)). Settled by: the running game.
- **On the options screen:** a click on a volume lighting the cells up to
  the mouse, and dragging along it ([systems/screens.md](systems/screens.md)).
  Settled by: the running game.

- **On the screens:** a button shows its "selected" picture while it's
  held; the empire screen's Accept does nothing until an empire is
  chosen; clicking an emblem chooses its empire
  ([systems/screens.md](systems/screens.md)).
  Settled by: watching them in the running game.
- **The colour key is magenta** (31, 0, 31 in 15-bit colour): the
  executable sets it, and magenta is what the pictures leave undrawn
  ([formats/wdf.md](formats/wdf.md)). Settled by: no picture showing pink
  where the game shows none.

## Settled

- **November 2026:** Save Game's chosen slot (pink text, the slot lit)
  and its cursor, from a screenshot ([systems/screens.md](systems/screens.md)).

- **November 2026:** Save Game's title and its look before a slot is
  chosen, and the glyphs' last row left undrawn, from a screenshot
  ([systems/screens.md](systems/screens.md), [formats/fnt.md](formats/fnt.md)).

- **November 2026:** the game setup screen's colours, from a screenshot:
  the values and timers as the options screen's, the levels (90, 207, 140)
  ([systems/screens.md](systems/screens.md)).

- **November 2026:** the empire screen from a screenshot of the Klingons'
  description: the wrapping (a word and the space after it, but for the
  last word), the emblems `es_?icn1`, the text's colours, and all text a
  pixel below its line's top ([systems/screens.md](systems/screens.md)).

- **November 2026:** the race letters `h` Federation and `f` Ferengi, the
  fonts' advances (the cell's width + 1), from screenshots of the Hall of Fame and Load Game drawn
  by Reluva from the game's files ([formats/fnt.md](formats/fnt.md),
  [formats/stbof-res.md](formats/stbof-res.md)).

- **November 2026:** the options' default volumes (100, and 127 for
  video), and `Stbof.ini` keeping the options and the last game's setup,
  from the user's `Stbof.ini` and `last.sav`
  ([systems/game-files.md](systems/game-files.md)).

- **November 2026:** a saved game's archive and `gameInfo`'s description,
  checked on the tutorial's `game0.sav` ([formats/sav.md](formats/sav.md)).

- **November 2026:** Load Game from a fresh install's screenshot: slot *n*
  is `game`*n*`.sav`, its file name in the name field, the tutorials in the
  first four, the text's colour, `ls_svlb.tga` as its title, and Accept
  dimmed until a slot is chosen. The options' volumes as a fresh install
  has them ([systems/screens.md](systems/screens.md)).

- **November 2026:** the options screen from a screenshot: Tool Tips
  where the bar's pictures are named `3dacc`, its words and their colours,
  Random Events as YES, the timers' "None", the volume bars, and the
  screen over the dimmed main menu ([systems/screens.md](systems/screens.md)).
  With it, the layouts' font colours as stored red, blue, green
  ([formats/wdf.md](formats/wdf.md)).

- **November 2026:** how dim the main menu draws buttons that can't be
  used (35%) and every button behind a panel (28%), measured on the
  recording against the `mc*.tga` pictures
  ([systems/screens.md](systems/screens.md)).
- **November 2026:** the main menu dimmed behind the game setup and empire
  selection screens, from the recording.
- **November 2026:** the text boxes' alignment (their fonts' first two
  flags) and the game setup screen's galaxy pictures, from a recording
  ([systems/screens.md](systems/screens.md)).
