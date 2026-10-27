# The interface: the bar, the inventory, the scrapbook, the cards

What *Murder on the Orient Express* draws over its scenes besides the
dialogue box: the bar along the top of the screen (the bag, the train's
five cars, the hat), the inventory window with its six pages (the items,
examining one, combining, the scrapbook, the passports, the fingerprint
cards), the item held as the cursor, and the passport or fingerprint card
a script holds up (`startpass`, `startprint`).

## How this was worked out

All of it comes from `motoe.exe` (English 1.0), read in a disassembler to
make Reluva work with the game. The objects and the addresses read:

- **The screen** (`[0x498818]`, built at `0x414170`): its per-frame update
  `0x414400`, its drawing `0x414e40`, the clicks `0x416000` (left),
  `0x416450` (right), the keys `0x4164e0`, the fade `0x415850`/`0x4158e0`,
  the cursor `0x415650`.
- **The bar**: its pictures loaded at `0x4142fd` (`topmenubar01.png`,
  `topmenubar02.png`), its slide `0x417120`, drawing `0x417290`, the
  section under the mouse `0x4175c0` (bounds at `0x48d650`), which cars
  can be travelled to `0x417630`/`0x4176a0`, where the player is
  `0x417760`, a click `0x4177f0` (jump table `0x417b80`).
- **The inventory window** (`[0x49881c]`, built at `0x4190d0`): opening
  and closing `0x4194d0`, the update `0x419990`, drawing `0x419a90`,
  clicks `0x419d30`. Its pages share a base class (vtable `0x482874`:
  the tabs `0x41cc90`, the description bar `0x41cae0`, the slider's blip
  `0x41cb50`). The pages, by their vtables: the items `0x482834`
  (`0x41ac20`...), examining `0x4827d0` (`0x41a020`...), combining
  `0x482794` (`0x418110`...), the scrapbook `0x482948` (`0x41e390`...),
  the passports `0x4828c4` (`0x41cdc0`...), the fingerprint cards
  `0x482904` (`0x41d660`...). Positions are the floats after each
  vtable (`0x482808`.., `0x48286c`, `0x4828ac`.., `0x4828fc`, `0x48293c`..,
  `0x482980`..).
- **An item** (`0x420630`, `0x1628` bytes): read from `Text\itemlist.txt`
  at `0x420a70`; given at `0x41bc10`; its pictures there.
- **The cards** held up: `0x417ba0` (start), `0x417e10` (update),
  `0x417ea0` (the steps, jump table `0x41802c`), `0x418090` (end), the
  pictures' fade `0x40eda0`.
- **Text**: the screen's text call `0x414720` (font, rectangle, colour,
  text, alpha, flags: 1 centred, 2 right, 4 centred down, 8 at the foot,
  `0x1000` struck through), which breaks lines at spaces (`0x414c30`).
  Fonts and colours are `Text\fonts.txt`'s, by their place in the list
  from 0.

`reluva-tool awe-shot` pictures of Reluva's screens are compared by eye
with the pictures the game's code places.

## When the interface is there

`interface off` sets `[0x4986f4]`; `interface on` clears it. The interface
(`0x4143c0`) is there when the runner isn't in its `+0xe8` state, a scene
is shown whose `SHOW` record is on (scene `+0x14f0`, 1 if there's none;
read at `0x435d1a`), and `[0x4986f4]` is clear. `SHOW` is the editor's
"Show Interface": an `ac2-dump` sweep finds it 0 in 20 of the 394 scenes,
the menus and the close-ups (Sirkeci's market, Belgrade, Hubbard's room
at night). Without
it, the bar and the inventory aren't drawn or clicked, and I doesn't open
the inventory.

The player can act (`0x415fa0`) when the screen isn't fading, nothing is
loading, the runner isn't busy while locked, there's no dialogue box, no
message is up, and no scrapbook picture is shown full screen.

## The bar

Two pictures of the whole bar, 1024 wide: `topmenubar01.png` (dark) and
`topmenubar02.png` (lit), at (0, 0). It's cut into seven sections at x
75, 250, 422, 595, 771, 950 (and 1024): the bag, the baggage car, the
restaurant car, the salon car, the Calais coach, the Athens-Paris coach,
the hat.

**Showing it** (`0x417120`, each frame). Its alpha goes from 0 to 255 at
1000 a second (a quarter of a second), and back:

- While the inventory is open and fully faded in, it's hidden at once.
- Otherwise only when the player can act and no script is running. With
  the mouse below y 60 it fades out. With the mouse above y 5 it fades
  in; between 5 and 60 one that has started coming in goes on coming in.
- Fully shown, the mouse belongs to it down to y 60 (`0x417580`), else
  down to y 5: the scene doesn't see the mouse there.

While the inventory fades, the bar is drawn no brighter than 255 less the
inventory's alpha.

**Drawing** (`0x417290`). Each section is clipped out of one of the two
pictures: lit if it's the bag or the hat with the mouse on it, or a car
that can be travelled to, or the car the player is in. Under the car the
mouse is on, lit, its name (`map1`..`map5` in `Text\interface.txt`:
Baggage Car, Restaurant Car...) in font 6 and colour 11, centred in the
section's width between y 45 and 60; and if it's the car the player is
in, "You Are Here" (`map6`) in colour 12, centred in the section between
y 10 and 50.

**The mouse on it** (`0x4175c0`): only when it's fully shown, above the
picture's foot (y 71), and on an opaque pixel of `topmenubar01.png`; the
section is the first whose bound is past x. The bag and the hat are
always on; a car only when travelling is allowed at all (not before act
04A, not in 08A, not outdoors (`location exterior.forest | location
exterior.train`), not in `calais.9_ashes.puzzle`, and not in the baggage
car in 04A before `tag michellockbaggage`), and then:

| Car | On when |
|-----|---------|
| Baggage | `act >05c, act <08a, tag baggageunlocked`, or `act 04a, !tag michellockbaggage, tag seenmichel04a, tag micheladmonish` |
| Restaurant | `act 04a, tag micheladmonish`, or `act >05b, act <08a` |
| Salon | `act 04a`, or `act >05b, act <08a` |
| Calais | always |
| Athens-Paris | `act >05c, act <08a, tag athensunlocked` |

The player is in a car when the location's name has `baggage`,
`restaurant`, `salon`, `calais` or `athens` in it.

**A click** (`0x4177f0`; then `select1.wav`): the bag opens the inventory;
the hat is the in-game menu (as P: `fade out, menu ingame, fade in`); a
car that's off does nothing but the sound; the player's own car isn't
the bar's (the click goes on to the scene); another car drops what's
running and runs, by where the player is (with an item in hand, a click
on the bar is a use on nothing, below):

| Car | From | Script |
|-----|------|--------|
| Baggage | anywhere | `fade out, scene baggage_01 1 0, fade in` |
| Restaurant | the baggage car | `fade out, scene restaurant_01 1 0, fade in` |
| | elsewhere | `fade out, scene restaurant_06 2 0, fade in` |
| Salon | the baggage or restaurant car | `fade out, scene salon_01 1 0, fade in` |
| | elsewhere | `fade out, scene salon_06 2 0, fade in` |
| Calais | the Athens coach, `!tag athensunlocked` | `fade out, scene calais.10-11_10.02 4 0, fade in` |
| | the baggage, restaurant or salon car | `fade out, scene calais.hall_01 1 0, fade in` |
| | elsewhere | `fade out, scene calais.hall_06 2 0, fade in` |
| Athens-Paris | the baggage, restaurant, salon or Calais car | `fade out, scene athens.hall_01 1 0, fade in` |
| | elsewhere | `fade out, scene athens.hall_06 2 0, fade in` |

## The inventory window

**Opening and closing** (`0x4194d0`): the bag, I (when the interface is
there and there's no dialogue box), or a right click when the player can
act. With the window closed and an item in hand, they only put the item
back in the bag. Opening shows the items page; it fades in at 1000 alpha
a second. Closing ends the new item's blinking and fades it out; the item
in hand stays in hand, to be used. A conversation that starts closes it,
unless it was started from the inventory (combining, `gen_cantuse`...);
one that isn't `gen_` or `com_` also puts the item in hand back. Opening
a menu does both too.

**The frame**: `inventory\interface\windowblank.png` (608×608) at x
(1024 − 608) / 2 = 208, y 10. Each page draws its own picture, the same
size, over it at the same place. Changing pages cross-fades: the frame at
full, the old page fading out, the new fading in.

**The tabs** (`0x41cc90`): seven buttons down the frame's right, at
(x + 511, y + 43 + 78i), 67×60: the items, examine, combine, the
scrapbook, the passports, the fingerprint cards, close. The button under
the mouse gets `smallhighlight.png` at its corner plus (1, 1); a page's
own big button (examine's eye, combine's gears) `largehighlight.png`
(tab number 7). The highlight changing plays `mouseover2.wav`; a tab
clicked plays `select2.wav`. Any click in the frame is taken.

**The description bar** (`0x41cae0`): text in font 6, colour 6, centred
both ways in (x + 45, y + 520)–(x + 465, y + 568).

**Paging** (`0x41cb50`): pages that hold more than fit have arrows on the
line across their top, (x + 46, y + 29)–(x + 76, y + 44) back and
(x + 435, y + 29)–(x + 464, y + 44) on (`select1.wav`, round from the end
to the start), and `blip.png` on the line between, at x + 78 +
i × 342 / (n − 1), y + 29, which can be dragged (`mouseover1.wav`) to the
nearest page.

### The items (`windowinventory.png`)

Sixteen boxes a page, 4×4, each 96×96, from (x + 38, y + 54), 112 apart.
An item keeps its slot (0, 1, 2...: page = slot / 16, box = slot mod 16);
a new one takes the lowest free slot, and the window turns to its page.
There's always one empty page after the last used one (2 to 10 pages).
Items in the bag (state 2) show their `inv<item>.png` (or `.jpg`) at the
box's corner plus 5; one just taken (state 1) blinks with the caret
(every 0.33 s) for 5 seconds of the items page being shown, then stays;
only one blinks at a time.

- The mouse over an item shows its description (`itemlist.txt` column 2).
- A click on an item picks it up: it's in hand (state 3), its sound plays
  (`sound\items\<item>.wav`, or `default.wav`; not for passports and
  fingerprint cards), and the cursor becomes its `cur<item>.png`.
- With an item in hand, a click on an empty box puts it there (its sound),
  and on another item swaps them (that one is in hand).

### Examine (`windowexamine.png`)

The item in hand, else the one examined last, else the first. Its
close-up is its `inv` picture's path with `\inv` made `\exa`, or `\ex2`
for its back; without one, the `inv` picture. It's centred in (x + 40,
y + 56)–(x + 472, y + 376). The arrows and blip go through the items in
slot order (those in the bag, just taken or in hand).

- The text: on the back, the Flipped Description (column 7) if it has
  one; else the Examine Description (column 5).
- The eye, (x + 220, y + 411)–(x + 292, y + 476), turns the item over if
  it has a back (`select1.wav`). Turned to the back with an Examine Event
  (column 4: `(!has keypass2), talk com_hut 8, take keypass2`),
  `discovery.wav` plays and the event runs.
- A click on the picture, nothing in hand, picks the item up.

### Combine (`windowcombine.png`)

Four boxes on the left, (x + 56, y + 56 + 112i), 95×95; the result's box
(x + 360, y + 224)–(x + 454, y + 319); the gears, (x + 221, y + 240)–
(x + 292, y + 303), between. A filled box shows its item's `inv` picture
and `x.jpg` at its top right corner (right − 21, top + 1; the result's
right − 20, top + 2).

- A click on a box with an item in hand puts it there (the item's sound;
  it goes back in the bag, and stays in the box). A click on an X
  empties the box (`select2.wav`). A click on a filled box picks its item
  up.
- The gears (`0x418a50`; `combine.wav`):
  - With items on the left and none on the right: if two or more, the
    row of `Text\combinelist.txt` whose Parts are exactly those items,
    that Can Combine and whose Condition holds. The parts are used up,
    the Results given (just taken; the first that isn't a fingerprint card
    put in the result's box) and the Event run, or `talk gen_combine`.
    With no such row: "These items cannot be combined." (`combine2`) and
    `talk gen_cantcombine`, or for the ice tongs and the paper scrap
    `talk gen_icetongs, tag triedicetongs`.
  - With only the result's box filled: decombining, the row whose Result
    is that item, that Can Decombine and whose Condition holds: the item
    used up, the Parts given into the left boxes, the Event or `talk
    gen_decombine`. Else `combine3` and `talk gen_cantdecombine`.
  - Both: `combine1` ("Either the source or destination slots must be
    empty...").
- The text: the description of the item under the mouse, else the
  message.

### The passports (`windowpassports.png`)

Opening the page moves the passports from the bag to it (state 5). Twelve
a page, 6×2, their `fol<item>.jpg` from (x + 45, y + 61), 72 across and
70 down, 62×62 each. One clicked (its sound) is shown above, its `exa`
picture centred in (x + 40, y + 216)–(x + 472, y + 489), with its Examine
Description in the bar. 1 to 3 pages (over 12 and 24 passports).

### The fingerprint cards (`windowfingerprints.png`)

Two rows of cards, each a carousel: the people's (on top) and the prints
lifted from things (below: the cards whose Examine Description says
whose print it matches). The card shown is at (x + 191, y + 56) (the top)
and (x + 191, y + 320), its neighbours at x + 40 and x + 342; each its
`inv` picture with its name (column 2) in font 12, colour 14, centred at
the foot of (card x + 5, y)–(x + 120, y + 136). The top row pages with the
usual arrows and blip; the bottom one with the same 449 lower.

The magnifier between, (x + 225, y + 232)–(x + 286, y + 287) (tab number
8, the small highlight), compares the two: if the lower card's Flipped
Description is the upper card's name, a tag `<upper>=<lower>`
(`printmatteo=printpadlock`) is set and `talk gen_fingerprint 6` runs;
else `talk gen_fingerprint 7`. Once a lower card has its tag, its Examine
Description ("Matteo's print matches...") is in the bar.

### The scrapbook (`windowscrapbook.png`)

`Text\Scrapbook\*.txt`, read again on each change. Text is in font 4
(journal text), colour 13 (journal), from (x + 50, y + 60), a row's
height plus 5 apart, a new page when a row would end past y 525. A row is
shown if its Condition holds as the page is read (`0x41f030`); the
instructions' tick is the same with their Checkmark Condition.

- **Contents** (`contents.txt`): the shown Sub-Headings, one under
  another; a click opens that section (`select1.wav`): clippings,
  instructions, suspects, timetable, maps, documents, Barnaby, the
  challenge.
- **Clippings, suspects, maps, documents**: first the list of their
  Sub-Headings (each once). For the suspects, `passicon.png` at text
  x + 225 if Poirot has their passport, `printicon.png` at x + 250 if
  their fingerprint card. A click opens the entry:
  - a clipping on `newspaperbase.png` at (x + 41, y + 56): its five rows
    are the date (font 11, right-aligned in (40, 40)–(392, 56) of the
    paper), the headline (font 9, or 10 if it takes more than two lines,
    centred in (30, 56)–(402, 112)), the sub-headline (font 10, centred
    in (192, 110)–(392, 192)), the photograph (`Inventory\Scrapbook\`,
    at (40, 110)) and its caption (font 11, (40, 264)–(176, 304)), all in
    colour 14;
  - a suspect: their rows' Entry (Name, Nationality...) in a column as
    wide as the widest plus 25, then Entry2 wrapped to x + 460;
  - a map or a document: its picture (`Inventory\Scrapbook\<Entry>`)
    full screen, centred, over the scene darkened (alpha 192). Any click
    puts it away.
- **Instructions**: each shown Entry, wrapped to x + 460, struck through
  once ticked.
- **Timetable**: the time, then the Entry wrapped to x + 460, from twice
  the first time's width.
- **Barnaby, the challenge**: each shown Entry, Antoinette's in colour 12.

The arrows `scraparrowbackward.png` (x + 41, y + 520) and
`scraparrowforward.png` (x + 440, y + 520) turn the pages
(`mouseover1.wav`); back from a section's first page is its list, or the
contents.

## The item in hand

The cursor (`0x415650`) with an item in hand: if the object under it
would take the item (`0x419e90`: a conditional script asking for it holds,
or `uselist.txt` has it for the character), the operate cursor; else
`cursorempty.tga` with the item's `cur<item>.png` at the mouse plus 26.
A click uses it ([script.md](script.md#items-in-hand)); a click on nothing
is `talk gen_cantuse 1`, the item staying in hand.

## The cards held up

`startpass <who>` and `startprint` (`0x417ba0`) show a card in the middle
of the screen: for a passport, `inventory\items\pass<who>\exapass<who>.png`
alone; for a fingerprint card, `card.png` (500×500) with `print.png` over
it. They're centred at x (1024 − w) / 2 and y (768 − h) / 2, or 80 higher
with the dialogue box up (y (768 − h − 160) / 2). They don't slide; they
fade:

1. The screen darkens to half (black at alpha 128), 8 every 0.01 s.
2. The card fades in, 8 alpha every 0.025 s (0.8 s). A passport's
   `startpass` is done as it starts (`passport.wav`).
3. A fingerprint card's `print.png` fades in when the card is in;
   `startprint` is done then (`fingerprint.wav`).
4. `endpass`/`endprint` fades the card (and print) out, then the screen
   back from half dark, and is done when it's clear.

The darkening and the cards are drawn over the inventory and the bar,
under the dialogue box and the cursor.

## In Reluva

`src/engines/awe/scene_interface.cpp` does all of the above in the Scenes
page, in a game. The item states, slots and the page shown are the
`GameState`'s (`itemSlots`, `itemPage`, `fresh`), set by `addItem` as the
game's `0x41bc10` sets them, and saved with the game (each item's slot
and page, as `0x41c730` writes them). The item in hand is drawn at the
mouse; the cursors themselves aren't drawn yet. F9 shows Reluva's info
panel in a game (I is the inventory's).

`reluva-tool awe-shot` shows the screens without a window:
`start:slot12` starts a game at a developers' save, `run:<script>` runs a
script (`run:startpass michel`), `click:x,y` and `rclick:x,y` click.
`reluva-tool awe-acts -explore` combines items too.
