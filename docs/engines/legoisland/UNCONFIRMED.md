# LEGO Island: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- The settings' yes-or-no values are the text "YES" for yes, and
  `savepath` is a folder the game's saved games go in: Reluva sets it to
  the install folder ([systems/running.md](systems/running.md#in-reluva)).
  Settled by what `CONFIG.EXE` writes, seen in a registry it ran on.
- In an SI object, flag `0x08` is a still's transparency and the
  medium's 1 or 0 whether the game manages its palette: the jukebox's
  script sets both together, so they may be the other way round
  ([formats/si.md](formats/si.md#mxob-an-object)). Settled by the
  game's code, or by which stills draw a transparent colour.
- In `WORLD.WDB`, an ROI is placed by the animation node of its name:
  its first move, then its first turn (a quaternion w, x, y, z), after
  its parents'; an ROI taking a part's LODs names the part, perhaps
  with digits after; a mesh's transparency is one minus its alpha, and
  its shading byte flat (0) or smooth (1). The data viewer's models come
  out whole drawn so ([formats/wdb.md](formats/wdb.md#a-model)). Settled
  by the game drawing them the same.
- An SI animation's nodes go from key to key in straight lines (round
  the sphere for turns), placed move, turn, scaling, after their
  parents'; a model actor is the model of its name without `*` or
  digits; a character is the shared part's pieces named by its nodes;
  the camera is at its first list's point looking at its second's. The
  viewer's scenes come out whole and moving so
  ([formats/obj.md](formats/obj.md#behaviour)). Settled by the game
  playing them the same, in a recording.
- An imported game runs with `diskpath` and `cdpath` both its folder:
  the import puts the CD's scripts there. To be settled when the game
  first runs ([systems/game-files.md](systems/game-files.md#importing-it)).

## Guessed

None yet.

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
