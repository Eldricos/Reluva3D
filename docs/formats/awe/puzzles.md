# Puzzle screens

Three scenes of *Murder on the Orient Express* are puzzles with code of
their own: the torn letter, the baggage car's safe and the puzzle box.
Each scene's `PUZZ` record names its puzzle, which the game creates on
entry and deletes on leaving.

The puzzle object takes its input in this order:

1. Each left click goes to the puzzle **first**, then the hotspot under
   the cursor runs its script.
2. Every left-button release goes to the puzzle, wherever the cursor is.
3. The scene's scripts drive it with `action` commands and ask it with
   `action` conditions.

None of the three takes keys. Each screen is left by its strip along the
bottom.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the scene entry that creates the puzzles (`0x436d00`);
- the three classes: the torn letter `0x43ec00`, the safe `0x434390` and
  the puzzle box `0x433290`.

The torn letter's solution was checked by compositing its pieces in place
over `paperfull.jpg`.

## The torn letter (`Athens.12-13_12.Puzzle`)

**The pieces.** There are 20, `paper01.png` to `paper20.png`.

- The first time, they're shuffled among the places the scene file gives
  them, using the C library's `rand`. Each is turned 0, 90, 180 or 270
  degrees.
- Each piece's place is kept in a text tag named after it (`paper01.png`
  = `"x y z turn"`), with the highest depth in `topz`. These go into saved
  games.

**Picking up and dropping**

- A click picks the front-most picture with an opaque pixel under it,
  tested with the piece turned. Hotspots come first, so the arrows and the
  exit hide what's under them.
- A piece picked up is tinted (blue × 0.75), brought to the front, and
  follows the mouse.
- Dropped with its centre inside (491, 18)–(992, 648), it snaps to a 4×5
  grid. The cell centres are at x 554/680/805/931 and y 82/208/334/460/586.
- Dropped outside, its centre is kept on the screen.

**Turning.** `action left` turns the picked piece +90° about its centre,
and `action right` turns it −90°. The arrows' conditions hold only while
a piece is picked.

**Solving.** Piece i belongs in column i mod 4, row i div 4, upright. It
counts within 5 pixels of the cell centre (one pixel right of where it
snaps) and within 15 degrees. Then the game runs `fade in paperfull, tag
solved_tornletter, talk gen_tornpaper 2`, and the pieces' tags are
removed.

## The safe (`Baggage_Safe.01`)

**The dial.** Pressing the dial's left half turns it one number up
(`action left`), and the right half one number down (`action right`).

- Each step is 3.6°, with a click sound.
- Held, the dial goes on after 0.25 s, each step 0.75 times the last,
  down to about 30 numbers a second.

**The combination is 6-6-6:**

1. left to 6;
2. right a full turn back to 6;
3. left a full turn back to 6.

**Entering it**

- A number counts when the button is let go on it, turning the expected
  way.
- After the third, the game runs `sound safeclick, talk gen_safe 2`.
- Pressing the wrong way after the first number starts again (`talk
  gen_safe 1`).

**Opening.** `action open` holds once the three are in, and the handle's
hotspot then opens the safe (`tag safe_solved`).

**State between visits.** The game writes `safecombo_dial`,
`safecombo_index` and `safecombo_dir`, but reads `safe_dial`,
`safe_index` and `safe_dir`, which nothing writes. So the dial is at 0 on
every visit.

## The puzzle box (`Calais.14-15_14.puzzle`)

**Drawing.** The box's pictures have the condition `0`; the puzzle draws
them itself at (89, 130), in this order:

1. the lid (`top.png`);
2. the side (`side.png`, `sideup01.png`, `sideup02.png`);
3. the near end, whose picture depends on its two halves (`end.png`,
   `endtl.png`, `endtr.png`, `endbl.png`, `endbr.png`, `endtlbr.png`,
   `endtrbl.png`, `endtlbrdown.png`).

The picked piece's arrows are drawn too, lit under the mouse when that
move is possible. The end's arrows sit 50 pixels up for the upper half
and 50 down for the lower.

**Picking a piece.** A click picks a piece by the opaque pixels of its
picture:

1. the end: its upper half inside the outline of the spot named
   `endtop`, else its lower half;
2. the side;
3. the lid.

**Moving it.** With a piece picked, a click on one of its arrows moves
it, and any other click drops the selection.

- A half of the end can't slide further the way it has already gone; that
  move does nothing.
- A right move plays a slide sound.
- A wrong move plays `talk gen_puzzle 4`. The third wrong move in a row
  starts the box over with `talk gen_puzzle 3`.

**The solution**, with pairs in either order:

1. the upper end right and the lower left;
2. the side up;
3. both halves back to the centre;
4. the side up again;
5. the upper left and the lower right;
6. either half down;
7. the lid forward.

The lid then opens (`topopen.bik`, the side drawn over it within
(440, 0)–(1023, 436)), and the locket follows: `take locket`, `tag
puzzlebox_solved`.

**State between visits.** Kept in seven tags: `puzzlebox_moveindex`,
`_endupper`, `_endlower`, `_side`, `_top`, `_arrowpiece` and `_misses`.

## In Reluva

`scene_puzzles.cpp` follows the above, in a game and when browsing
scenes. Supporting changes:

- **Moved pieces.** Pictures a puzzle moves and turns are drawn by
  overrides of their place, turn, depth and tint.
- **Showing objects.** `show` and `fade in <object>` put an object up
  whatever its condition. A video shown starts from its first frame.
  `hide` and `fade out <object>` take it down again.

To check against the game: whether `left` turns a torn-letter piece
clockwise on screen. The game adds 90° to the D3DX roll, and Reluva draws
that as clockwise.
