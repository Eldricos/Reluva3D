# Star Trek: Starfleet Command II: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **Models' levels of detail are finest first.** The data viewer shows
  the first MOD0 as the finest; each model's later levels have fewer
  triangles, and its first two are sometimes alike
  ([formats/mod.md](formats/mod.md)). The game itself drawing them so
  would settle it.
- **The high half of a broken model's vertex indices is its piece**
  ([formats/mod.md](formats/mod.md)); the viewer ignores it. Seeing a
  ship break up in the game would settle it.

## Guessed

- **Models aren't mirrored in the game.** The data viewer turns their z
  round, taking their coordinates as Direct3D's, left-handed
  ([formats/mod.md](formats/mod.md)). A screenshot of a ship in the
  game, against the viewer's, would settle it.
- **A string-table line with a single `|` is a key with no text**
  ([formats/text-tables.md](formats/text-tables.md)): one line of
  `aaStrings.txt`. What the game does with it is unknown.

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
