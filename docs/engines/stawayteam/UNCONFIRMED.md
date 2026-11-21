# Star Trek: Away Team: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

- **A rotation sequence's frames go a direction at a time,** each
  direction's together. Seen in the data viewer, where the models turn
  smoothly so; the game's drawing of them isn't traced
  ([formats/binary-object.md](formats/binary-object.md#behaviour)).

## Inferred

- **A key or a click skips a movie:** the game ends one on anything in
  its queue of input, and what goes into the queue isn't traced
  ([systems/startup.md](systems/startup.md#the-opening-movies)).
- **The menus:** the confirm dialog and the other windows in the
  middle of the screen, the credits fading to see-through, the toolkit's
  brightness scaling colours, Normal as the level to start with,
  `Button Action` played on a picture button's click, Esc on
  the main menu asking to quit, a button's sound on the click, the shortcut's
  line shown once Alt is pressed, a fixed-width font's cell, and the
  sounds named `.wav` being the `.ogg` files of their names
  ([systems/menus.md](systems/menus.md#open-questions)).
- **A frame drawn by its origin:** its top left at the point less its
  origin, as the sequences' bounds have them
  ([formats/binary-object.md](formats/binary-object.md#behaviour)).
- **Which fields are objects.** The game knows it from each class's
  properties; Reluva takes a field for an object when a `{` line follows
  it. The same on all of the game's files, but a file edited by hand
  could tell them apart
  ([formats/text-object.md](formats/text-object.md#behaviour)).
- **Blended pixels' alpha.** A frame's block 1 is taken as each blended
  pixel's alpha, 255 opaque, and its pixels as RGB 5:6:5: the pictures
  look right so, but the game drawing them hasn't been compared
  ([formats/frm16.md](formats/frm16.md#blended-pixels)).
- **Objects held in place: their classes.** Taken from the text files,
  which name them for 55 such properties; the other 24 are told by their
  version and size, and none of those is in the game's binary files
  ([formats/binary-object.md](formats/binary-object.md#properties)).
- **The `.RDS` scripts' rules** where the files don't show them (text
  before a label, a label twice): the game's reader isn't traced
  ([formats/rds.md](formats/rds.md#open-questions)).
- **The length of a few sounds.** For 5 of 27 sampled sounds, libvorbis
  1.3.7 gives 128 samples (6 ms) more at the end than FFmpeg does
  ([research/file-survey.md](research/file-survey.md#sound)). Reluva
  keeps libvorbis's, taking the reference decoder to cut the last block
  as the game's own `vorbisfile.dll` (February 2001) did. A recording of
  one of them played by the original would settle it.

## Guessed

- **The main menu's movies and song loop,** the movies without sound
  ([systems/startup.md](systems/startup.md#open-questions)).
- **Points' 8 bytes as two whole numbers,** not two floats, in what
  Reluva shows of an object
  ([formats/binary-object.md](formats/binary-object.md#properties)).

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
