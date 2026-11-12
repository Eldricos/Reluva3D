# Clue: Murder at Boddy Mansion: the game's screen

> **Status:** done · **Code:** `src/engines/clue/mansion.h`, `src/engines/clue/play.h` · **Checked against:** `Clue.exe` 2.2's code, and recordings of the original

## Overview

The game's screen, as `Clue.exe` lays it out. Its windows are Hasbro's
EagleM.dll's (`EGuiWindow`): 0x435050 makes them, the bar's at 0x43BD10
and its layout at 0x43C2D0, and 0x435630 places them, each from the
edges of the screen and of those already placed and the size of its
picture. The positions below are what that code gives when it runs, in
an emulator, with EagleM's calls stood in for (sizing a window to its
picture, moving it, giving its rectangle). They match what a 640×480
recording of the original shows, to the pixel, but for three it had
one pixel off.

## How it works

### Layout

- The frames: the top one (0, 0); the left and right ones (0, 5) and
  (635, 5), 408 high, down to the bar.
- The bar (0, 413), 640 × 49: the left ornament (0, 413), the notes and
  cards button (39, 413), the board button (84, 413), the six portraits
  from (141, 413) each against the last (60 wide: frame 0 as they are,
  1 whose turn it is, 2 not playing), the logo (501, 413), the right
  ornament (594, 413). Over each portrait, 47 in, a network game's
  lights (`menu:I...Light`).
- The ticker (0, 462), 640 × 18, its sentence in a strip from 41 to 599;
  `UI:IrtBtmOrnNorm` (599, 463).
- The drawer of a human's actions (511, 413), out over the logo, its
  three icons at (535, 420), (574, 420) and (606, 420): the die
  (`UI:Arolldiebtn`, then `UI:Arolled` with the number, or `UI:Anoroll`
  with the moves left, then the suspect's pawn `UI:A<sca>end` to end the
  turn), the bulb, the handcuffs.
- The right column: the videos' frame (`UI:Asuggframe`, 208 × 408,
  black edged in white) at (427, 5), the clip in its middle; the choice
  window (dialog 30) at (434, 4), against the right-hand frame and the
  bar, and a human's choice of card (dialog 32) where it is; the card
  shown, large (dialog 33, `UI:Algcard`, one frame a card), at (398, 5).
  The box saying who can or can't disprove (dialog 31, with that
  suspect's portrait) is placed as dialog 30 (not in that code).
- The windows over the game, 231 × 395: the exit window (dialog 44) at
  320 less half its width across, in the middle between the top frame
  and the bar down, (205, 11). The Esc menu, its Save window and the
  loser's window are placed by EagleM itself: in recordings of the
  original ("The Classic", the longplay, Playthroughs 01 and 02, their
  frames checked by where the bar's ornaments fall) the Esc menu, its
  Save window and its Preferences (dialog 37) are at (205, 11) too. The
  loser's window isn't in them; Reluva puts it there as well.
- The Esc menu's Preferences and How to Play are windows of that size over
  the game too, not the menus' screens: dialog 37 (the same switches and
  sliders, its buttons numbered from 1 and its sliders 29 and 30, Continue
  0), 38 (the seven topics, Continue 7) and 39 (the topic's text, scrolled
  by 0, 1 and the knob 2; Back 5 to the topics, Continue 4). The game goes
  on behind them. Reluva shows them so.
- The left column: the cards window and the notepad at (5, 5). The cards
  in it (0x43A600's table, 0x43A913) go at (106, 241), (13, 241), (106,
  124), (13, 124), (106, 7), (13, 7) from the bottom right; the code adds
  that to each card's rectangle on the screen and gives the sum as one
  inside the window, so the window's place counts twice: on the screen,
  (116, 251) and so on.
- The board from above (`UI:Aoverhead`) draws itself (class at
  0x43CF90); it is placed where the recording shows it, (208, 5), its
  squares 13 pixels, column 9 and row 9 at (8, 15) in it.
- The solution (dialog 34) fills the screen.

### What stands in front

As `Clue.exe` 2.2 does it (read from its code;
the recordings, OriginalClue 0:02-0:36 and L 3:00-3:30, show it). The
house is drawn solid, near walls and all. Each frame the suspect whose
turn it is (its flag at +0xD08, set with the turn by 0x425160) and each
number of a planned move (class Number, 0x429CD0) look at what is near
(0x428220, from slot 0x74 with 13 and 13): every object whose box on the
floor, less 8 units on its far sides, overlaps the 208 units (13 × 16)
beyond the actor's anchor along both x and y (0x428190). "Translucency"
on draws those see-through (their draw flags 0x30001, about half); off,
it leaves them out (flags 0). Only these kinds of object do it, each by
its class (the primary type picks the class, 0x4190B0; slots 0x68 and
0x6C): walls (0), secret passages (8), drapes (12); furniture (3)
unless it has a timing at 2000 (0x42A96C); the hot spots (4, 5) only
when the actor's square is a corridor (0x42A0A6). Floors, suspects,
numbers and the rest never. A suspect in a room does it to the walls
only (the flag 0x428080 gives, by its square's zone, 0x42A210). The
pieces are whole objects: walls come a square long, so the faded part
looks rectangular.

At the start (0:00-0:07) the lounge's armchair to the right of the
fireplace (`Olgosch_cd`) looks solid, though by the code it is in front
of Miss Scarlet. Drawn as the code does see-through (half its colours,
half the lit rug's, below), Reluva's looks the same: both are faded.

### The notepad

The notepad's picture (`UI:A<sca>notepad`) is ruled: each kind's rows 15
pixels apart below its line (22, 131 and 240), the columns of the colour,
the two marks and the triangle 15 wide from 131. The names, colours,
marks and triangles go in its cells.

### Lighting Effects

As `Clue.exe` 2.2 does it (read from its code and
`ImageRLE16.dll`'s). The lights are the actors that show what stands in
front of them (above): the suspect whose turn it is and the numbers of a
planned move; nothing else gives light (not the wall lamps, nor the
fires). Each frame every object starts unlit, then each light, from the
middle of its box (a suspect's 8 units in, a number's square's middle),
reaches what is anchored in the 128-unit cells within 240 units of it
and one cell further, but for what it shows through; each object keeps
the nearest light that reached it (0x428220, 0x4287C0). The shade at a
point (0x4289C0): d = max(dx, dy) + min(dx, dy) / 2 from the light;
full within 80, then a step darker for each 16 on: 7/8, 13/16, 3/4,
11/16, 5/8, 9/16, 1/2, 7/16, 3/8, 5/16, and 1/4 from 240 on; an object
no light reached is 3/16 (the default 0x110000, set at 0x4176CB). An
object is shaded at the middle of its box; a lit wall a shade for each
16-unit slice (each its own layer, 0x42B5D0); a lit floor of 4 × 4
tiles a shade for each tile (0x42AF70; a lit floor of another size isn't
drawn). The shades are `ImageRLE16.dll`'s pixel functions (from its
`GetBlittersTable`), taking each colour's 5 bits down by shifts and
subtractions (level 6, 7/8: c − c/8 ...); see-through is its function
3, half the picture's colours and half what is behind.

Before this was read, the lighting was measured on recordings (VM 0:21
and 0:28, VM2 0:42-1:12): full near a suspect, falling to about 21%,
and a pool of light following the squares of a planned move as they are
clicked. The code accounts for both: 21% is the far shades' 1/4 and
3/16, and the planned squares are lights of their own. Set beside
OriginalClue at 0:02, Reluva's frame now matches it to within a few
levels of 255 across the view.

### Fonts

The fonts' glyphs are drawn by their cells (an EIMG 0x100 glyph's cell
width, a 0x200 glyph's image width), as the menus and dialogs show them:
the Preferences screen's labels measure 103, 111, 91 and 92 pixels in
the longplay at 640×480 (L 1:02), Reluva's 105, 115, 92 and 94. The
ticker's sentence is drawn packed, each glyph's ink a pixel after the
last's and a space its cell and a pixel: "Miss Scarlet, it is your turn."
is 169 pixels in the longplay (L 3:00) and the VM recording, 189 by
cells, 172 packed.

## Sources and checks

- **Where the facts come from:** `Clue.exe` 2.2's code (the layout's run in
  an emulator, as above) and `ImageRLE16.dll`'s, and recordings of the
  original.
- **How it was checked:** against recordings of the original: the
  positions to the pixel but for three, the lighting to within a few levels
  of 255, the fonts' widths.
