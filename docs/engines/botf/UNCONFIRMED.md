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
- **How dim the game draws buttons that can't be used** (35%) and the
  main menu behind a panel (80% of that): figures that match every button
  the recording shows, but not measured on the game's own pictures
  ([systems/screens.md](systems/screens.md)). Settled by: the `mc*.tga`
  pictures against the recording.
- **The button under the pointer is drawn with its second picture:** the
  recording shows it lighter ([systems/screens.md](systems/screens.md)).

## Guessed

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

- **November 2026:** the main menu dimmed behind the game setup and empire
  selection screens, from the recording.
- **November 2026:** the text boxes' alignment (their fonts' first two
  flags) and the game setup screen's galaxy pictures, from a recording
  ([systems/screens.md](systems/screens.md)).
