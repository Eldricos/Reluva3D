# Star Trek: Armada: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

- **Textures:** read from `Textures\RGB\`, each with its mipmap levels
  `.1`, `.2`... until one is missing, in the original hosted in the
  emulator at 640 × 480 ([formats/textures.md](formats/textures.md#behaviour)).
  Not yet: which folder in other display modes.

## Inferred

- **A model's transforms:** a node's 3 × 3 matrix applied to a point as
  a row, then its translation, then its parent's: measured from where
  the sprite nodes land ([formats/sod.md](formats/sod.md#behaviour)). To
  settle: a ship seen in the running game against the Models page.
- **Levels of detail:** the higher a `lod<n>` node's number, the more
  detailed, from the vertices in all 83 models that have levels
  ([formats/sod.md](formats/sod.md#behaviour)).
- **Object definitions:** a key's last definition counts, so a file's
  own override what it includes ([formats/odf.md](formats/odf.md#behaviour)).
- **Sprite tables:** `@auto` keyframes laid out as cells along a row,
  down a column or in a square, by the tables' comments and the textures
  ([formats/spr.md](formats/spr.md#animations)).

## Guessed

None yet.

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
