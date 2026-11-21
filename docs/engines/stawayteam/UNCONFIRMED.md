# Star Trek: Away Team: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **Which fields are objects.** The game knows it from each class's
  properties; Reluva takes a field for an object when a `{` line follows
  it. The same on all of the game's files, but a file edited by hand
  could tell them apart
  ([formats/text-object.md](formats/text-object.md#behaviour)).
- **Blended pixels' alpha.** A frame's block 1 is taken as each blended
  pixel's alpha, 255 opaque, and its pixels as RGB 5:6:5: the pictures
  look right so, but the game drawing them hasn't been compared
  ([formats/frm16.md](formats/frm16.md#blended-pixels)).
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

None yet.

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
