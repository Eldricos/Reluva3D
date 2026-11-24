# LEGO Loco: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

- **A frame set's delay counts the game's 28 ms timer ticks, and its
  restart delay seconds:** the viewer plays frame sets so. It rests on
  the timer, the main loop and the stepper (`0x405c40`); timing an
  animation in the original would settle it
  ([systems/drawing.md](systems/drawing.md#animation)).

## Seen running, not traced in the code

- Why the station master talks slower in the original than his 10 ms
  timer asks: Windows' timer messages coming less often, by its clock
  ([systems/drawing.md](systems/drawing.md#animation)).

## Inferred

- **A square is 16 pixels, and a piece's picture stands on the bottom of
  its squares:** the viewer draws boards so; it rests on the pieces'
  grids and pictures and on the boards coming out whole. A screenshot
  of the original's board would settle it
  ([systems/drawing.md](systems/drawing.md)).

## Guessed

- **A board naming no backdrop has `backdrop.bmp`:** the viewer draws it
  so ([systems/data-viewer.md](systems/data-viewer.md)).

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
