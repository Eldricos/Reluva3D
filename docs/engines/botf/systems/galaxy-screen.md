# Star Trek: Birth of the Federation: the galaxy screen

> **Status:** partial · **Code:** `src/engines/botf/ui/galaxy_screen.cpp` · **Checked against:** screenshots of the English retail game at a new game's first turn, and at a second game's eleventh, as the Federation on small galaxies · **Confidence:** confirmed unless marked

## Overview

The screen a game is played on: the galaxy's map in the middle, with the
turn and summary buttons, the empire's figures and its task forces down
the left, the map's modes along its bottom, and the selected system along
the screen's bottom. Each empire has its own layout, `?maingal.wdf` by
its letter (`hmaingal.wdf` for the Federation), and its own pictures.
Reluva draws the frame, the map, the empire's panel and the strip from a
saved game ([formats/sav.md](../formats/sav.md)), opened by Load Game's
Accept or the main menu's Continue; what the rest of the screen does is
to come.

## How it works

### The layout

`hmaingal.wdf`'s widgets, by their place and pictures:

| Widget | Place | What |
|---|---|---|
| 1001, 1006 | 63, 6 and 63, 42 | the Turn and Summary buttons (`h_turn`, `h_summ`) |
| 1021 | 63, 76, 100x30 | the turn: "TURN 1", in `human28.fnt` |
| 1027, 1003, 1005, 1004, 1002 | along 432, from 275 | the map's modes: Overlay, Economic, Events, Military, Map; Military lit at the start |
| 1017 | 249, 36, 526x391 | the map |
| 1020 | 28, 110, 196x380 | the empire's panel and its task forces (`h_eicon.wdf`, `h_tfinfo.wdf`) |
| 1014 | 7, 459, 786x134 | the selected system (`h_starmp.wdf`, `h_sysmap.wdf`) |
| 1026, 1025 | 273, 198 and 564, 353 | the overlay's choices (`h_ovrlay.wdf`) and the zoom's (`h_zoom.wdf`, [below](#zooming)) |
| 1038, 1039, 1040 | 9 to 33, 154 to 499 | the task forces list's scroll bar: not shown with the first turn's two |

The turn is in (24, 101, 189), the options screen's blue, measured; the
words are `lexicon.dic`'s ("Turn", entry 1370, in capitals). The Overlay
and Map buttons' pictures mark what isn't drawn with yellow, not magenta
([formats/wdf.md](../formats/wdf.md#pictures-and-transparency)). Drawn
so, the frame, its buttons and the turn match the screenshot pixel for
pixel, but for the pointer.

### The map

Measured on the screenshot, a small galaxy of 13 by 10 sectors:

- **The grid:** sectors 39 pixels square, in the middle of the map's
  place (from 258, 36), with dark lines between them. Their pixels, nearly
  black and blue, don't follow the backdrop's under them, nor any picture
  found; Reluva draws them in (0, 0, 8), most of them within the 16-bit
  screen's rounding.
- **The backdrop**, `hm_glmap.tga` (526x391, the map's size), is drawn
  from the grid's corner, 9 pixels into the map's place, and cut to the
  grid: beyond its right and bottom edges the place stays as the screen's
  background has it. Between the lines it matches the screenshot pixel
  for pixel where the scanners don't reach.
- **What the empire's scanners reach:** at the first turn the 3 by 3
  sectors round Sol, darker, with the grid dotted in (24, 73, 173). Their
  pixels don't follow the backdrop's either: about a third as bright on
  average, but not pixel by pixel. Reluva darkens the backdrop to a fifth,
  which brings 72% of their pixels within 12 of the game's *(guessed: the
  game's way isn't known)*. The save doesn't hold
  it: its explored flags have only Sol's sector. Reluva counts a sector
  round each of the empire's systems and task forces *(inferred: the
  ranges of scanners, and of ships, are the data tables')*.
- **Stars:** each system in the scanners' range is drawn with its star's
  small picture (`s-y-maps.tga` for a yellow star, 10x10, black its
  background), not the 19x19 `s-y-map.tga`. In its sector it's placed by
  its place on the galaxy's small map (`systInfo`, 5 steps a sector), 7
  pixels a step less 3: Sol (2, 3 into its sector) at 11, 18, its
  neighbours Thelka (2, 2) at 11, 11 and Ramatis (3, 2) at 18, 11, each
  matching the screenshot's stars pixel for pixel. The systems of the
  sectors the empire has explored are drawn too, in range or not (the
  eleventh turn's screenshots); the game also shows stars in sectors
  neither in range nor explored, by what it remembers of them, which isn't
  read from the save yet.
- **Names:** a system the empire has explored has its name under its
  star, centred, in `human9.fnt`: Sol's, but not Thelka's or Ramatis',
  which are only in range; at the eleventh turn, the nine systems explored,
  and only those. The empire's own systems' are in (0, 251, 255), those
  nobody holds in (255, 186, 57); Reluva draws the other empires' and the
  minor races' in the second, as none is seen *(guessed)*.

- **Ranges:** green, yellow and red lines, a pixel wide, along the sector
  edges round what's within a short, medium and long range of the
  empire's systems, the shorter drawn over the longer, closed by the
  grid's edges. A sector counts as within a range when the larger of its
  column and row distances from Sol, and half the smaller rounded down,
  add up to no more than 1 (green), 2 (yellow) or 5 (red): that gives
  every border edge on the screenshot, the red's staircase included
  *(inferred: why those ranges, presumably the empire's ships' reach,
  isn't known)*. In (0, 255, 0), (255, 255, 0), (255, 0, 0). Along the
  galaxy's top edge the border is a pixel lower, inside the grid's line,
  and over the selected system's square (Sol's, at the eleventh turn).
- **The selected system:** a square in (247, 125, 0), a pixel inside its
  sector's lines; Sol is selected at the start. A click on a sector
  selects it.
- **Task forces:** the empire's small emblem, `i_hi18.tga` (17x21), a pixel
  right of the sector's corner and a pixel above it, once for the sector,
  drawn over the square.

### Zooming

The Map button (1002) opens the zoom's two buttons, `h_zoom.wdf` in its
place (1025, at 564, 353) over the map's corner: "SMALL MAP" (1022,
`h_zin.tga`) zooms in, "LARGE MAP" (1023, `h_zot.tga`) out, as the
user's screenshots of the eleventh turn are named. Zoomed out is the map
above, the whole of a small galaxy. Measured on the zoomed-in screenshot:

- **The view** stays the zoomed-out grid's square, lines included (258
  to 765 across, 36 to 426 down): what falls outside it isn't drawn
  (Neutron Star D2's name is cut at 258), and the backdrop stays where it
  was, unscaled (a patch of it matches at the same place).
- **Sectors** are 61 pixels square. The grid is placed with the selected
  sector's corner at the view's middle (511, 231), and moved no further
  than keeps it over the whole view: Sol's, in the galaxy's top row, puts
  the grid's corner at 84, 36. The borders and the selected square land
  on the screenshot's pixels so.
- **Stars** are their larger pictures (`s-y-map.tga`, 19x19), 12 pixels a
  step of their small-map place less 8: all 22 stars in view agree. Names
  are as zoomed out, under them.
- **Task forces:** the empire's larger emblem, `i_h26.tga` (26x30), a pixel
  right of and below the sector's corner (two task forces agree).

When the buttons close, and which are lit, isn't seen: Reluva closes them
on a choice or a click elsewhere, lights the Map button while they're
open and the current zoom's button *(guessed)*.

### The empire's panel

Down the left, in the panel's place (28, 110), four layouts one under
another, measured on the screenshot: `hinfobox.wdf` at its top,
`h_eicon.wdf` 90 down, `hinf_tf.wdf` 130 down and a `h_tfinfo.wdf` row
for each task force from 147 down, 40 apart.

- **The empire's information** (`hinfobox.wdf`): the "FEDERATION INFO"
  bar is its button's picture (`h_info.tga`). Its text box (1018, at 45,
  140, `human15.fnt`) has three lines 17 pixels apart (the font's 16 and
  one): "TOTAL CREDITS" and the credits, "CREDITS PER TURN" and the
  credits a turn with its sign, and the dilithium a turn with its sign,
  "DILITHIUM" 6 pixels after it (so after "+1", and after "0" in another
  screenshot), and the last turn's events, "No Events". The words are in
  orange (247, 121, 33), the figures in green (173, 227, 115), the
  figures ending 6 pixels short of the box's right edge; the words are
  `lexicon.dic`'s (entries 1361, 194, 239, 833). The figures are the
  empire's (`empsInfo`, [formats/sav.md](../formats/sav.md)).
- **The other empires' emblems** (`h_eicon.wdf`): `i_c26.tga`,
  `i_f26.tga`, `i_k26.tga`, `i_r26.tga` (26x30), each at the top of its
  place (1050 for the Cardassians to 1054 for the Romulans; the player's
  has none), across its middle.
- **The task forces' bar** (`hinf_tf.wdf`): its backing picture,
  `hm_bktfi.tga`, is black but for a tab at its left, and the game shows
  the background through the black; its tab is `hm_tab22.tga` where the
  layout names `hm_tab20.tga` (in the screenshot; `hm_tab32.tga` is the
  same picture).
- **A task force's row** (`h_tfinfo.wdf`): the empire's emblem
  (`i_h26.tga`) across the middle of its place, and two lines in purple
  (99, 77, 181), 17 apart: its mission ("ENGAGE" for 6 and "AVOID" for 5,
  `lexicon.dic`'s 658 and 652) and its first ship's class
  (`shiplist.sst`'s name: "Science Vessel", "Colony Ship"), in the order
  `GWTForce` lists them.

Drawn so from the save, the panel matches the screenshot pixel for pixel,
and the screenshot five turns on (2,449 credits, +92). Two screenshots
of a game loaded from the first save show "2 Events", and one "0
DILITHIUM" with a destroyer queued, where Reluva shows what the save holds
and "No Events" *(not known yet: the events, and the dilithium the queue
uses)*.

### The strip

Along the screen's bottom, in place 1014 (7, 459), the selected sector:
a system's strip (`h_sysmap.wdf`), a stellar object's (`h_starmp.wdf`),
or, for an empty sector, nothing at all (not even the strip's frame,
`h_sysbkg.tga`, which both layouts have for their window's picture).
Measured on screenshots of Sol, Babel, Manark and a neutron star:

- **A system's star and planets** turn in the planets' place (1020, at
  207, 477, 548x100), from their `.ani` files
  ([formats/ani.md](../formats/ani.md)): the star's (`s-y-.ani` for
  `s-y-`) at the place's right end, then the planets leftward from the
  innermost, as `systInfo` lists them, each 16 pixels from the last; all
  with their middles on the place's (527). Sol's nine planets would run
  past the place's left end so: its are 11 pixels apart, the outermost's
  left edge at 228. Reluva closes the gaps a pixel at a time until the
  outermost is at least 21 pixels inside the place *(guessed: only Sol's
  needs it)*. Babel's three, Manark's seven and Sol's nine land on the
  screenshots' pixels, each at one of its frames.
- **How fast they turn**, counted in recordings: a planet's 24 frames in
  4.75 seconds (about 198 milliseconds a frame), a star's 36 in about 1.2
  (33), the neutron star's 8 in about 1.2 (150).
- **Each planet's class letter**, under it, centred across its frame, in
  `human9.fnt`: by the planet's type (`systInfo`), 0 P, 1 J, 2 G, 3 B, 5
  O, 6 M, 7 Y (seen on the three systems' 19 planets; 4 L *(guessed)*).
  It's blue for a gas giant (type 3), green for a planet that's
  terraformed (its terraforming done reaches what it needs, both 0 for
  most of Sol's) and red for one that isn't, in (0, 0, 255), (0, 255, 0)
  and (255, 0, 0): which fits all 19.
- **A system's texts**, in `human15.fnt`: "SYSTEM" in the title box (1100,
  246, 459) in orange (247, 121, 33), the system's name 53 pixels after
  it in green (173, 227, 115); who lives there in box 1101 (611, 459), in
  orange: "HUMANS" for Sol, "UNINHABITED" for Babel and Manark (entries
  1214 "system" and 1388 "uninhabited" in capitals, and 445). Then four
  lines 17 apart: "POPULATION", "MAX POP", "GROWTH RATE" and "ORBITALS"
  (919, 626, 425, 891) in orange in box 1102, and the population and the
  maximum population in green, right-aligned in box 1103. The texts match
  the screenshots pixel for pixel.
- **A stellar object's strip:** its animation 300x100 in the middle of
  its place (1020, at 226, 477); its name ("Neutron Star I6", the save's)
  in orange in the title box and "Description" (228) in box 1101; and its
  description, `lexicon.dic`'s entry after its name's (811 for "Neutron
  Star", 810), wrapped to box 1103 (532, 477, 220 wide), a line every 17
  pixels, in (165, 121, 33). The sixth line runs past the box's foot.
  The neutron star's strip matches its screenshot pixel for pixel. The
  black hole's, the X-ray and radio pulsars' and the wormhole's are drawn
  the same way with `blkh.ani`, `xp-.ani`, `rpls.ani` and `worm.ani` and
  their entries, by the files' and entries' names *(guessed)*.
  A nebula has no animation, and no strip is drawn for it yet.

Not drawn yet: the growth rate and the orbitals' figures ("3.1%" and
"0/0" for Sol at the first turn, "0.7%" for Babel), which no field of the
save seen holds; the icons over some planets (a red flare, a green plant)
and over Sol's star (a blue crystal, presumably its dilithium); the
empire's emblem at the strip's left for its own systems; and the people's
word for a race other than the Federation's (only "HUMANS" is in capitals
in `lexicon.dic`).

## In Reluva

`GalaxyScreen` (`src/engines/botf/ui/galaxy_screen.cpp`) loads the
player's empire's layout, and draws its frame, the map's backdrop, grid,
the scanners' sectors, stars and names, the turn, and the empire's
panel, and the selected sector's strip, turning. The engine opens it
with the chosen slot's save from Load Game, or with `Auto.sav` from
Continue; a click on the map selects a sector, and the Map button's two
buttons zoom; Escape goes back to the main menu. The map is drawn on a
surface the view's size, which cuts it there. The unit tests
(`botf.unit.screens`: "galaxy") cover the scanners' range, where stars and
names are drawn, the panel's parts, the strip (the star's and planets'
places, their frames in time, the letters, a click's selection, the
neutron star's strip and an empty sector's), and zooming: the grid's
place round the selected sector, the stars and emblems, the cut, a
click, and the buttons.

Drawn so, the map matches the first turn's screenshot but for the grid
lines' and the scanned sectors' shading. At the eleventh turn, zoomed in
and out, every pixel of the game's borders and selected square is
Reluva's too, and the stars and names drawn land on the game's; what the
map shows of later turns isn't all drawn yet (see the open questions);
the strip matches but for what isn't drawn yet, and the
outermost pixels of the larger planets' rings, which the game seems to
blend with what's under them.

## Sources and checks

- **Where the facts come from:** `hmaingal.wdf` and its pictures in the
  English `stbof.res`; a screenshot of the game by the user, November
  2026, at the first turn of a new game (Federation, small galaxy, few
  minor races, Simple, random events off), with its save. For the strip,
  the `h_sysmap.wdf` and `h_starmp.wdf` layouts and the `.ani` files, and
  the user's screenshots and recordings of a second game at its eleventh
  turn (Federation, small ring galaxy), with its save: Babel, Manark, an
  empty sector and a neutron star selected, Sol turning.
- **How it was checked:** drawn by Reluva from that save, the frame and
  turn match the screenshot pixel for pixel, and the map away from what
  isn't drawn yet to within the 16-bit screen's rounding: the backdrop
  where the scanners don't reach, the three stars and Sol's name. The
  strip, drawn from the second game's save with each sector selected,
  matches as above, each planet and star at the frame it shows.

## Open questions

- How far an empire's scanners reach, and why only Sol's sector is
  explored: the systems' and ships' scan ranges (`shiplist.sst`), and
  when a sector counts as explored.
- What the ranges are and what sets them, and the grid lines' and
  scanned sectors' shading.
- What the map shows at later turns: the stars of sectors seen before,
  the darker sectors (not those Reluva counts as in range), the dotted
  green, blue and red sectors, stellar objects' pictures and the other
  empires' ships.
- When the zoom's buttons close, and which of them is lit.
- The grid lines' exact shade, which varies a little with the backdrop
  in the screenshot.
- Larger galaxies: whether the map scrolls zoomed out, and whether the
  zoomed-in map scrolls (none is seen).
- The strip's growth rate, orbitals and icons, the empire's emblem on it,
  a nebula's strip, and the people's word for the other races.
- The panel's events, and its dilithium with a ship queued; the words for
  the other missions; a task force's second line when it has several
  ships.
