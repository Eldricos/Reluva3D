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

Measured on screenshots of small galaxies of 13 by 10 sectors, at the
first turn of one game and the eleventh of another, with their saves
(`sector.lst`'s fields: [formats/sav.md](../formats/sav.md)):

- **The grid:** sectors 39 pixels square, in the middle of the map's
  place (from 258, 36). Its lines aren't drawn: they are the screen's
  background (`hm_scrn.tga`) showing, every pixel of them at the eleventh
  turn.
- **The backdrop**, `hm_glmap.tga` (526x391, the map's size), is drawn
  from the grid's corner, 9 pixels into the map's place, but only inside
  the sectors the empire hasn't seen. A sector it has seen ("revealed",
  `sector.lst`'s 0x30, or explored) shows the screen's background too,
  which is darker: every pixel of the seen sectors at the eleventh turn is
  `hm_scrn.tga`'s. At the first turn those are the 3 by 3 sectors round Sol.
- **What the empire's scanners reach now** (its scan strength, at 0x40,
  above 0) has the lines round it dotted in (24, 73, 173): every other
  pixel from each sector's corner, so from the grid's corner in one sector
  and a pixel on in the next (39 is odd). A sector whose strength is 0 or
  less, the row above Sol's at the first turn, under a nebula, keeps its
  lines plain.
- **Claims:** a seen sector an empire claims (0x0E) is dotted in its
  colour, on every other row from the sector's second: the Federation
  (16, 8, 115) and the Klingons (107, 8, 8) a dot every 3 pixels, one
  pixel further left each time; the Romulans (8, 89, 8), the Cardassians
  (115, 8, 90) and the Ferengi (156, 89, 0) a dot every 4, two pixels
  further each time (the thirty-fourth turn's screenshot has all five). A
  sector two empires claim (one seen: the Cardassians' and the Romulans')
  is striped on every row instead: along x + y, every 10 pixels, five
  stripes, (16, 8, 115) at 1, (107, 8, 8) at 3, (115, 113, 8) at 5,
  (115, 8, 90) at 7 and (8, 89, 8) at 9: not the claimants' colours
  alone, so Reluva stripes any contested sector so *(inferred)*.
- **Outposts:** the empire's own, a pixel right of the sector's
  bottom-left corner, at its foot: `i_ho11.tga` (11x16), the Federation's
  two at the thirty-fourth turn. The other empires' outposts in seen
  sectors aren't shown (`starBaseInfo`, [formats/sav.md](../formats/sav.md)).
- **Stars:** each system in a seen sector is drawn with its star's small
  picture (`s-y-maps.tga` for a yellow star, 10x10), not the 19x19
  `s-y-map.tga`. In its sector it's placed by its place on the galaxy's
  small map (`systInfo`, 5 steps a sector), 7 pixels a step less 3: Sol
  (2, 3 into its sector) at 11, 18, its neighbours Thelka (2, 2) at 11, 11
  and Ramatis (3, 2) at 18, 11, as every star on the screenshots. What
  isn't drawn is the picture's top-left pixel's colour: black, but for
  `s-o-maps.tga`, whose top-left pixel is a dim orange and whose black the
  game draws (Ramatis and Umoth).
- **Stellar objects** in seen sectors: their small map pictures
  (`nsmaps.tga` for a neutron star), centred where a star's would be
  (Neutron Star D2's and I6's). The others' pictures go by their files'
  names (`blkhmaps.tga`, `xp-maps.tga`, `neb-maps.tga`, `wormmaps.tga`,
  `rplsmaps.tga`) *(guessed)*.
- **Names:** what the empire has explored has its name under its picture,
  in `human9.fnt`: Sol's, but not Thelka's or Ramatis', which are only
  seen; at the eleventh turn, the nine systems explored and Neutron Star
  D2. A name starts at the picture's left edge plus half of its width
  less the name's and one, rounded down: every name on the screenshots,
  of odd widths and even, zoomed in or out, but Sol's zoomed in, which the
  game draws a pixel further left, for a reason not known. The 9-point
  fonts set their glyphs a pixel closer than the others
  ([formats/fnt.md](../formats/fnt.md)). The empire's own systems' names
  are in (0, 251, 255), those nobody holds in (255, 186, 57), a Romulan
  system's in (0, 170, 16) (Romulus, in a zoomed-in screenshot of the
  forty-fourth turn). Reluva draws the other empires' and the minor
  races' in the orange, as none is seen *(guessed)*.

- **Ranges:** green, yellow and red lines, a pixel wide, along the sector
  edges round what's within a short, medium and long range of the
  empire's systems and outposts (the thirty-fourth turn's green borders
  are round Sol and two outposts), the shorter drawn over the longer,
  closed by the
  grid's edges. A sector counts as within a range when the larger of its
  column and row distances from Sol, and half the smaller rounded down,
  add up to no more than 1 (green), 2 (yellow) or 5 (red): that gives
  every border edge on the screenshot, the red's staircase included
  *(inferred: why those ranges, presumably the empire's ships' reach,
  isn't known)*. In (0, 255, 0), (255, 255, 0), (255, 0, 0). Along the
  galaxy's top edge the border is a pixel lower, inside the grid's line,
  and over the selected system's square (Sol's, at the eleventh turn).
  The ranges keep out of the sectors claimed by an empire whose territory
  the player has no access to (`empsInfo`'s, [formats/sav.md](../formats/sav.md)):
  at the thirty-first and thirty-fourth turns they stop at the Cardassians'
  and Romulans' sectors, whose territory the Federation's non-aggression
  treaties with them close to it, and run into the Klingons' and
  Ferengi's; at the eleventh, with access everywhere, into the Romulans'. Drawn so,
  every border edge of four screenshots of those turns but one matches:
  a red edge at the galaxy's left that the game leaves out, as if a range
  were measured along a path round the sectors left out *(not known
  yet)*.
- **The selected system:** a square in (247, 125, 0), a pixel inside its
  sector's lines; Sol is selected at the start. A click on a sector
  selects it.

Drawn so from their saves, the first turn's map and the eleventh's, zoomed
out and in, match the screenshots but for under a hundred scattered pixels
each, Sol's name zoomed in, and the pointer.
- **Task forces:** the empire's small emblem, `i_hi18.tga` (17x21), a pixel
  right of the sector's corner and a pixel above it, once for the sector,
  drawn over the square. Where one is ordered to (`GWTForce`'s 0x30, a
  sector's number: row by the width, and column), `movs.tga` in the
  middle of the sector (the eleventh turn's task force at Gamma, headed for
  I6). Others' task forces where the scanners reach: their small emblem,
  13 pixels wide, a pixel right of and below the corner, the same zoomed
  in: a monster's (owner 43, past `race.rst`'s 35 races; its ship "Echo
  Papa 607") is `i_t13.tga`. Reluva takes an empire's to be `i_?13.tga` by
  its letter and a minor race's `i_m13.tga` *(guessed)*.

### The sector line

Over the map, in text box 1081 (262, 8, 500x22, `human20.fnt`), the
sector under the pointer, nothing while it's off the map. Measured on
two screenshots ("Unexplored Sector i6" with "Scan Strength -3", and
"Unclaimed Sector i1" with "Scan Strength 5"):

- **Its name:** the column's letter, small, and the row from 1, in
  `lexicon.dic`'s "Unexplored Sector %s" (1385) where the empire hasn't
  explored it, "Unclaimed Sector %s" (1382) where no empire claims it,
  "Contested Sector %s" (187) where two or more do, and the empire's
  adjective before "Sector %s" (1107) where one does: "Romulan Sector d1",
  "Cardassian Sector c6" (122, 319, 323, 560 and 1058 for the five). In
  the turn's blue
  (24, 101, 189), in the box as its other text.
- **The empire's scan strength there** ("Scan Strength %d", 1096), ending
  6 pixels short of the box's right edge: yellow (255, 255, 0) above 0
  (1 and 5), red (255, 0, 0) otherwise (0, -3, -11, -43).

Drawn so with the pointer where the screenshots have it, the line
matches them pixel for pixel.

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
- **Sectors** are 61 pixels square. The grid is placed with a sector's
  corner at the view's middle (511, 231), and moved no further than keeps
  it over the whole view: Sol's, in the galaxy's top row, puts the grid's
  corner at 84, 36. The borders and the selected square land on the
  screenshot's pixels so. The sector is the one selected when zooming in,
  the map staying there as others are selected: a third screenshot, with
  Babel selected, has the grid at Sol's place, and in the recording a zoom
  from sector (1, 8) puts it at (258, -184), held at the galaxy's left and
  bottom edges.
- **Stellar objects** are their larger pictures (`nsmap.tga`, 16x16),
  centred on a star's place as zoomed out.
- **Stars** are their larger pictures (`s-y-map.tga`, 19x19), 12 pixels a
  step of their small-map place less 8: all 22 stars in view agree. Names
  are as zoomed out, under them.
- **Task forces:** the empire's larger emblem, `i_h26.tga` (26x30), a pixel
  right of and below the sector's corner (two task forces agree).

In a recording of the user's: a choice closes the buttons; the current
zoom's button is lit (Large Map zoomed out, Small Map zoomed in), the Map
button isn't while they're open, and the button under the pointer pulses
as the main menu's do. Reluva also closes them on a click elsewhere,
not seen *(guessed)*, and pulses the screen's other buttons alike
*(guessed)*.

### The empire's panel

Down the left, in the panel's place (28, 110), four layouts one under
another, measured on the screenshot: `hinfobox.wdf` at its top,
`h_eicon.wdf` 90 down, `hinf_tf.wdf` 130 down and a `h_tfinfo.wdf` row
for each task force from 147 down, 40 apart.

- **The empire's information** (`hinfobox.wdf`): the "FEDERATION INFO"
  bar is its button's picture (`h_info.tga`). Its text box (1018, at 45,
  140, `human15.fnt`) has three lines 17 pixels apart (the font's 16 and
  one): "TOTAL CREDITS" and the credits, "CREDITS PER TURN" and the
  credits a turn with its sign, and the dilithium a turn with its sign
  (but "0", unsigned and in the words' orange, when there's none),
  "DILITHIUM" 6 pixels after it (so after "+1", and after "0" in another
  screenshot), and the last turn's events: "2 Events" (entry 310) in the
  figures' green, ending where "No Events" (833) does, or "No Events" in
  orange, or "1 Event" (297). The count is the empire's results in
  `result.lst`, but for a new game's own task forces and ships (their
  value 0, [formats/sav.md](../formats/sav.md#results)): "2 Events" in
  the three screenshots of games loaded from the first turn's saves, "5
  Events" at the eleventh turn, "1 Event" at the thirty-first and
  thirty-fourth (a race met, its value 0 too, counts), "No Events" from
  the saves without the Federation's. The rule is incomplete: in the
  ShipBuild game's turn-2 save the Federation has four results, a ship
  built and its task force (military 5 and 4, value -1) and Sol's two
  (economic 0 and 8), and the game, loaded, shows "2 Events", listing
  "Destroyer built in Sol." and "Sol System queue empty" (the user); the
  Colony game's second save shows 2 of the same four kinds, its fourth
  "1 Event" of four (economic 31, military 4, 5 and 16), while the second
  game's eleventh turn counts all five of its, a ship and a task force
  among them. Reluva counts 4, 3 and 4 there. The games' Summary
  screens list the events (the user, November 2026), which ties results
  to them:

  | Save | The Federation's results (kind, content) | Listed |
  |---|---|---|
  | ShipBuild, turn 1 | military 5 and 4 twice (the starting ships, value 0), economic 17 twice | "Sol System has enough population for new trade routes." twice |
  | ShipBuild, turn 2 | military 5 and 4 (a ship built), economic 0 and 8 | "Destroyer built in Sol.", "Sol System queue empty" |
  | Colony, fourth save | economic 31, military 4, 5 (the colony ship used) and 16 | "Xerxes colonized" |
  | Second game, turn 11 | military 10, 4, 5 and 34, economic 31 | "Federation defeat Klingons", "IKC LoHwI' A destroyed", "Encountered Klingons in sector j5", "Ship destroyed by neutron star in d2", the home front's spirits raised by the victory |

  So economic 17 is a system ready for trade routes, 0 a ship built and
  8 a queue left empty, military 16 a colony founded and 10 a battle won;
  identical results are each listed. A ship's and its task force's
  results (5 and 4) are listed when it's lost, but not when it's built or
  used up founding a colony, and the colony's turn doesn't list its
  economic 31 either. Neither the results' headers nor their contents
  hold why they were made (the contents of all of them are laid out
  alike, the rest of each the game's memory left over): Reluva will know
  that once it plays the turns itself. The words are in
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
- **The rows** (`h_tfinfo.wdf`): what's in the selected sector, not every
  task force: at the thirty-first turn one of the Federation's four, the
  one at Deneb, selected; at the seventeenth a Klingon scout with the
  Federation's task force. Each row has an emblem across the middle of its
  place and two lines in purple (99, 77, 181), 17 apart:
  - The empire's outposts there first: its outpost emblem (`i_ho30.tga`),
    "Outpost" (894) and "OPERATIONAL" (58). One being built shows how far
    it's got instead ("27%" in the figures' green, "Complete" in the
    turn's blue), which isn't read yet.
  - Then the task forces, in the order `GWTForce` lists them. The
    empire's own: its emblem (`i_h26.tga`), its mission's word
    ("ENGAGE" 658 for 6, "AVOID" 652 for 5, and the others by `GWTForce`'s
    published missions: "BUILD OUTPOST" 653 for 7 and so on), and where
    it's headed elsewhere, "eta", the turns it takes and "TURNS" (293,
    1372): "ENGAGE eta 2 TURNS" for a science vessel (speed 1 in
    `shiplist.sst`) two rows away. The turns are taken to be the larger of
    the column and row distances over its slowest ship's speed, rounded
    up, from the one seen *(inferred)*. Its second line is its first ship's class
    (`shiplist.sst`'s name: "Science Vessel", "Colony Ship").
  - Another empire's task force: its emblem (`i_k26.tga` for the
    Klingons') and its first ship's class alone. The one seen is the
    selected task force, framed and its line in orange (255, 150, 16);
    Reluva draws others' in the purple *(guessed)*, and doesn't frame a
    selected one yet.

Drawn so from the saves, the panel's information and rows match the
screenshots of the first, eleventh, thirty-first and thirty-third turns
pixel for pixel. The
first turn's own screenshot, of the game as it started rather than
loaded, shows "No Events" where its save gives 2: Reluva always opens a
save, and shows 2. A screenshot five turns on shows "0 DILITHIUM" with a
destroyer queued, where Reluva shows the save's figure *(not known yet:
the dilithium the queue uses)*.

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
  pixels, in (165, 121, 33). A line takes a word only while it and the
  space after it stay narrower than the box ([formats/fnt.md](../formats/fnt.md));
  broken at a sentence's two spaces, the next line starts with the
  second. The sixth line runs past the box's foot. The neutron star's,
  the wormhole's (`worm.ani`, 1448), the radio pulsar's (`rpls.ani`, 969)
  and the X-ray pulsar's (`xp-.ani`, 1451) strips match their screenshots
  pixel for pixel, each at one of its frames; and so does the black
  hole's (`blkh.ani`, 95, "Black Hole K7" in the user's ShipBuild game),
  its five lines and its picture at one of its frames. A nebula has no
  animation, and no strip is drawn for it yet.

- **Bonuses:** an icon on a planet's frame's top, from its left edge: a
  red flare (`bie.tga`) on a desert or volcanic planet (types 2 and 7)
  with an energy bonus, a plant (`bif1.tga`) on an oceanic or terran one
  (5 and 6) with a food bonus (`systInfo`, [formats/sav.md](../formats/sav.md)):
  all 19 planets of Sol, Babel and Manark, Babel's terran planet, with no
  food bonus, having none. Jungle planets (4) are taken to show the plant
  *(guessed)*. A system with dilithium has a blue crystal (`bid.tga`) at
  its star's top-left corner (Sol's). These pictures leave out their
  top-left pixel's colour, black, as the map's do.
- **The empire's emblem:** a system an empire holds has its emblem
  (`i_h46.tga` for the Federation's) at the planets' place's top-left
  corner (207, 477) under the planets, at 64% of its brightness: every
  channel of Sol's emblem, at the 16-bit screen's 5 bits of red and blue
  and 6 of green, is the picture's times 0.64, rounded down. The other
  empires' are taken by their letters *(guessed)*.

- **Who lives there:** "HUMANS" for the Federation's people, a minor
  race's `race.rst` name in capitals ("BANDI" at Deneb); the other
  empires' people are taken to be their names in capitals *(guessed)*.
- **A planet's details:** with the pointer over a planet's frame, the
  four lines show the planet instead of the system, as the labels and
  figures do, not in capitals: its name and its type's word
  (`lexicon.dic`'s "Volcanic" 1414, "Terran" 1258 and the others), "Max
  Pop" and its maximum population (`planspac.bin`,
  [formats/planspac.md](../formats/planspac.md)), "Growth Rate" and its
  rate (not worked out yet), and "Terraform" with the terraforming done
  and needed ("0 / 440"), or once it's terraformed, who lives there:
  "Humans" for the Federation's people, a minor race's `race.rst` name
  ("Bandi", "Bolians"). Ten planets of Sol and Xerxes on the user's
  screenshots match pixel for pixel but for the growth rate. An
  uninhabited terraformed planet's last line is left empty *(guessed:
  none is seen)*.

Not drawn yet: the growth rate and the orbitals' figures ("3.1%" and
"0/0" for Sol, "0.7%" for Babel, "0.3%" for Deneb), which no field of the
save seen holds: for Humans the rate fits `race.rst`'s growth rate (10)
times the maximum population over 1,080, but Deneb's Bandi (0.7, 105)
don't. With those, the five systems' strips match their screenshots, each
planet at one of its frames, but for the larger planets' outermost
pixels.

### Tool tips

With the options' Tool Tips on, a control under the pointer shows its
tip from `teachtip.tdb`, under its widget's id
([formats/tdb.md](../formats/tdb.md)), drawn as the game menu's are
([screens.md](screens.md#the-game-menu)). Of the Federation's galaxy
screen's widgets, these have one: Turn (1001), Summary (1006), the five
modes (1027, 1003, 1005, 1004, 1002), the empire's panel's title bar
(1007), the system strip's population boxes (1102, 1103), and the scroll
arrows (1038, 1039), which aren't shown. The stellar object's strip's
description box has 1103's id too, and so the population's tip; Reluva
shows none there *(guessed)*. A tip that would run off the screen is moved
back onto it *(guessed)*, as the Map button's would off its right edge. That the galaxy screen's controls show
tips as the game menu's do, and how soon, isn't seen yet.

## In Reluva

`GalaxyScreen` (`src/engines/botf/ui/galaxy_screen.cpp`) loads the
player's empire's layout, and draws its frame, the map's backdrop, grid,
the scanners' sectors, stars and names, the turn, and the empire's
panel, and the selected sector's strip, turning. The engine opens it
with the chosen slot's save from Load Game, or with `Auto.sav` from
Continue; a click on the map selects a sector, the sector under the
pointer is named over it, the Map button's two buttons zoom, controls
show their tips; a right click opens the game menu, whose main menu button (or Escape) goes back to the main menu, the game still in progress, to save it or go on with it ([screens.md](screens.md#the-game-menu)). The map is drawn on a
surface the view's size, which cuts it there. The unit tests
(`botf.unit.screens`: "galaxy") cover the sectors scanned and seen, where
the backdrop, claims, stars, stellar objects, names, task forces and their
destinations are drawn, the panel's parts, the strip (the star's and planets'
places, their frames in time, the letters, a click's selection, the
neutron star's strip and an empty sector's), and zooming: the grid's
place round the selected sector, the stars and emblems, the cut, a
click, and the buttons.

Drawn so, the map matches the screenshots as above; the strip matches
but for what isn't drawn yet, and the outermost pixels of the larger planets' rings, which the game seems to
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
  turn match the screenshot pixel for pixel. The map, drawn from both
  games' saves, matches the first turn's screenshot and the eleventh's two,
  zoomed out and in, but for under a hundred scattered pixels each, one
  name zoomed in and the pointer. The strip, drawn from the second game's save with each sector selected,
  matches as above, each planet and star at the frame it shows.

## Open questions

- How the game works out the scan strengths and what's seen and
  explored, which Reluva reads from the save: the systems' and ships'
  scan ranges (`shiplist.sst`).
- What the ranges are and what sets them.
- The Ferengi's and Cardassians' claims' dots, and whether a sector
  claimed by two empires shows both.
- The one range edge that doesn't fit, and the treaties other than peace
  (alliances, war pacts) and their effect on the ranges.
- Which explored systems are named at later turns: at the thirty-third,
  nine explored sectors have no name (the Romulans' and Cardassians'
  systems among them, and both pulsars), and no field read tells them
  from the named ones; at the forty-fourth Romulus is named, but its
  neighbours Tyrus and Norpin still aren't, zoomed in too.
- How the growth rate is worked out. Seen: Sol 3.1% at populations 310
  and 335 (of 335), Romulus 2.9% (355), Babel 0.7% (uninhabited, 75),
  Manark 0.0% (uninhabited, 0), Bolarus 0.3% (42 of 50), and Deneb 0.3%
  at 32 and 0.9% at 220 a turn later, its planets unchanged, as the
  Bandi joined the Federation; there the screen shows 220 for the
  maximum, the save keeping 105. A planet's own rate is shown with the
  pointer over it: Deneb II 0.1% (60), Bolarus III 0.3% (50). A colony
  the Federation founded at Xerxes kept 0.2% as its population grew from
  10 by 1 a turn to 14, and showed 0.2% before it was founded: the rate
  doesn't follow the population. A system's maximum population is the sum
  of its terraformed planets' (Xerxes' 50 its small terran planet's alone,
  Deneb's 105 Deneb II's 60 and its desert planet's 45), which
  `planspac.bin` holds by type, size and bonuses *(published)*; the rate
  is presumably its planets' too, through `habit.bin` (by planet type and
  atmosphere, six figures each, presumably one for each empire and one for
  the minor races) and `ppgrowth.bin` (ten population ranges, from 10-50
  to 2000-10000, each with a figure falling from 1,000,000 to 1,250).
  A system's rate is its terraformed planets' added up: Sol's Mercury
  0.1%, Venus 0.2%, Earth 2.0% and Mars 0.8% make its 3.1%, and Xerxes'
  0.2% is Xerxes IV's alone. For Humans a planet's rate rises with its
  maximum population, faster than in proportion: 20 and 25 0.1%, 40 to 55
  0.2%, 65 0.4%, 75 0.7%, 85 0.8%, 140 2.0% (eleven planets of every
  type), but no rule tried fits them all yet, nor the other races'.
- Why Sol's name, zoomed in, is a pixel left of where the rule puts it.
- Whether a click elsewhere closes the zoom's buttons.
- Larger galaxies: whether the map scrolls zoomed out, and whether the
  zoomed-in map scrolls (none is seen).
- The strip's growth rate and orbitals, a nebula's strip, and the
  people's word for the other races.
- The panel's dilithium with a ship queued; an outpost's progress while
  it's built; the rows' look for a selected task force, another empire's
  and a minor race's; a task force's second line when it has several
  ships; "eta" for a single turn and for a plain move.
