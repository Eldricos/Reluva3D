# Clue: Murder at Boddy Mansion: playing

> **Status:** partial · **Code:** `src/engines/clue/play.h`, `src/engines/clue/rules.h`, `src/engines/clue/frontend.h` · **Checked against:** `Clue.exe`'s code, and recordings of the original · **Confidence:** from the code unless marked

## Overview

The game opens with its intro (a key or a click skips it), then its
menus, as the original. In the game, the arrow keys move the view
(**Shift**: faster) and **Esc** opens the game's menu: Save, Preferences,
How To Play, Exit To Main (which asks "Are you sure you want to exit the
current game?" first, the original's dialog 44), Continue. The game
doesn't stop for it, as the original's doesn't (its `EscapeDialog` sets
no pause; it waits in a loop that runs the game's own, 0x441590, and
only disables Save in network games, 0x440AD0): in the recordings
computer players play on behind it ("The Classic" 46:48-47:03, a whole
turn; Playthrough 01 4:45-4:50 and 17:25-17:32), and in network games so
does everyone else. The preferences
are saved in Reluva's folder for the game (`preferences.ini`). As the VM
recording shows them, and `Clue.exe` does ([screen.md](screen.md#what-stands-in-front)): "Translucency" on
draws what stands in front of the suspect whose turn it is see-through,
off leaves it out (the lounge's near wall gone between 0:57 and 1:09,
the same view); "Mansion Walls" off leaves out every wall in the house
and the lamps on them (its Wall class draws nothing then, at 0x42B5E6;
its furniture of the secondary type 1, the lamps' tag, neither, at
0x42A9AF), floors, furniture and pictures left (2:53 and 3:02).

## How it works

### In the game

As the original, from its recordings and its How to Play texts:

- **The die** (the drawer's first icon, at the bar's right end, on a
  human's turn): rolls; the icon shows the number. Then click the
  squares of the move one at a time, starting next to the suspect. Each
  is numbered on the floor as it's clicked (the squares it could go on
  to aren't marked, as in the original). Clicking one of the move's squares again goes
  back to it. When the die's number is reached, or a room's floor is
  clicked from before its door, the suspect walks there.
  As `Clue.exe` does it (0x423A90, a click on a square): the world keeps
  the level's nine `tilenum` objects (class Number) by their digit
  (0x429B60, at +0x540); putting a square in the plan shows the next one
  (0x429D00) at the position of the object the square's cell in the
  floor's grid holds (0x4209F0, 64-unit cells): for a corridor square its
  hallway floor piece, anchored on the square's corner (16 + 64 × column,
  16 + 64 × row), at height 0, and plays `ting.wav`; taking squares back
  hides theirs (0x429D60) and plays `unting.wav`, a square it can't go to
  `nogo.wav`. The suspect walks to the squares' middles (+32, +32).
  Reluva does the same.
- **About the room**: on your turn, in a room, a click on its floor
  (not a door's square, not one of the floor pieces with key 1100)
  walks the suspect there, round the furniture, and moves it nowhere in
  the game; a move being planned is dropped (`Clue.exe` 0x423B5B,
  0x424900). The way is `Clue.exe`'s way-finder's (0x41A0C0), over the
  house's 32-unit cells (their middles the points; blocked under things'
  boxes, 0x427FC0, not under the suspects): breadth first from the
  suspect's cell to the one clicked, stepping up, down, right, left, then
  up-right, down-right, down-left, up-left, to cells free and in the room,
  a slanting step only where neither cell it passes between is blocked;
  then back from the cell clicked to the first such cell a step nearer
  each time. The walk goes through those cells' middles. Reluva does the
  same.
- **No Die Roll**: the first icon shows the moves left; plan the move
  the same way and click its last square again (or the icon) to walk.
  Suggesting, accusing and the secret passages cost three moves.
- **Secret passages**: from a corner room at the start of a turn, click
  the passage in the room (or, on the board from above, the room it
  leads to).
- **The bulb** suggests, after entering a room (or when brought into one
  by someone's suggestion): choose a suspect, then a weapon, then
  Suggest. **The handcuffs** accuse, any time in the turn: a suspect, a
  weapon and a room. On the summary, click a picture to choose again;
  Cancel gives up.
- **Ending the turn**: once moved, the first icon is the suspect's pawn;
  click it.
- **The notes and cards** button (the bar's left) opens the cards in
  hand; the notepad icon under them opens the notepad, whose cards icon
  goes back. Each row has a colour box (click: each suspect's colour in
  turn, then none), two marks (click: tick, X, O, tick in O, X in O,
  none) and a triangle that opens a note to type beside the notepad
  (Enter closes it; the triangle shows when a row has one, and pointing
  at it shows the note). The notepad starts empty: in the recordings
  players colour their own cards in by hand. "Auto Notes" (off at first)
  does one thing, as `Clue.exe` reads it (0x437C9D, after a card is
  shown to a player, calling 0x43A070): the card's row in the colour of
  who showed it, and a tick in its first mark.
- **The board** button shows the board from above, with the suspects'
  pawns; moves can be planned on it too.
- **Suggestions**, as `Clue.exe` runs them: the suspect named is brought
  to the room unless already there, to a place found from where the
  suggester stands (0x41BEA0); the players are asked in turn from the
  next seat on, those without cards passed over, until one can answer
  or it comes back round (0x41873E). A human out of the game after a
  wrong accusation becomes one of its "LostHumanDoDisprove" or
  "LostHumanNoDisprove" players: disproving by hand, or left to the
  computer, as they choose.
- When a human must disprove, their matching cards are shown, as
  `Clue.exe`'s DisproveDlg does it (0x441C60, its clicks at 0x442250):
  one card in the middle on the disprover's background (`...Dis2Bg`),
  with Done; two or three in the window's other places on the other
  background (`...Dis3Bg`), and a click on one shows it at once, no
  Done. The window's "..., Please click on the card" lines it never
  shows. A card shown to a human is shown large, with OK.
- **The end.** A right accusation shows the solution; Continue plays the
  murderer's prison clip, then the main menu. A human who accuses wrongly
  while other humans play on is asked whether to go on disproving by
  hand or automatically (the solution isn't shown). When no human is
  left in the game, the solution is shown ("You have accused
  incorrectly", or "Everyone has accused incorrectly") and the game ends.

The computer players take their turns on their own, the camera following
them. The ticker keeps "..., it is your turn." through a human's turn;
the prompts ("please roll the die") are spoken. Which ones, as `Clue.exe`
has it: everything the butler says goes through one queue (0x438F80),
which takes a turn's "..., it is your turn" (voice 34 + the suspect, in
the table of names at 0x45E698), a prompt chosen from what the turn
offers (0x439140: the die alone, or with a suggestion, a passage or both,
voices 29-32; nothing when the die isn't offered), and the lines for
disproving and accusing (40-63). "Please move" (28), "end turn" (33, in
the UK's files alone) and "choose object" (64) are in the table but
nothing plays them, in 2.0 or 2.2. The recordings agree: in the three
with the butler's voice on, the die's prompts are heard 31 times in all,
each about 3 s after "..., it is your turn", and those three never.
A sentence stays centred
8 s, then scrolls left, coming round from the right. The suggestions'
videos play in the right column when "Suggest Movies" is on; the spoken
lines play when "Butler Voice" is on; "Walk Animations" off moves the
suspects at once; "Lighting Effects" darkens the house but around the
suspects.

The music, when the CD's tracks have been ripped into the game's folder
([music.md](music.md)): none in the menus with the storm, track 5 from the character
picker, the network's player setup and Preferences on, the loading's
own loop, then tracks 2, 3 and 4 in turn in the game, as `Clue.exe` has
it.

The cards window fills as the original's: in card order from the bottom
right, right then left, row by row upwards (Playthroughs 01 and 02,
both hands whole).

### Timings

As `Clue.exe` has them: a suspect walks 8 units a step,
a step each 66 ms, a square in 0.53 s (0x4253C3; the VM recording, about
0.6 s); each "can / cannot disprove" box shows 3000 ms (0x437503);
between turns the camera goes to the next suspect, 2000 ms, before "...,
it is your turn" (0x4181F8). A computer player waits before each step
until 2 has passed on a clock of whole seconds (1-2 s; 0x423775): before
suggesting at the start of its turn, twice before rolling (its turn's
start, then the roll), before moving; not before suggesting on
arriving. (After 45 s stuck in a step, it is moved on, 0x4218D0.) The
ticker scrolls in a control of the Eagle library (`EagleM.dll`); its
timing is the recordings'.

## In Reluva

### Not yet as the original

- Suspects walk straight from a door to their place in a room, where
  `Clue.exe` finds a path over its walk grid (0x41A0C0); the place is
  its (see [board.md](../formats/board.md)).

## Sources and checks

- **Where the facts come from:** recordings of the original and its How to
  Play texts, and `Clue.exe`'s code at the addresses given.
