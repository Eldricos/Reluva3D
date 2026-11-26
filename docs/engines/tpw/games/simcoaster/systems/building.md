# SimCoaster: building attractions

> **Status:** partial · **Code:** `src/engines/tpw/park/attractions.cpp` · **Checked against:** a recording, and Land of Invention's settings · **Confidence:** inferred unless marked

## Overview

The player buys rides, sideshows, shops, facilities and features in the
Buy window, then places them in the park. In the recording: the
Attractions menu's "Build Attraction" (2:36), the "Buy Ride" window
(2:43), its sideshows' tab, "Grab-a-Prize" at § 500 chosen, and its
footprint, see-through blue, following the pointer until a click
places it (2:55), its price in yellow under the money. A ride's queue
is laid next, from its entrance ([paths.md](paths.md#queues)).

## How it works

### What a world has

Each attraction is a folder of its world's, by its kind: `rides/`,
`sideshow/`, `shops/`, `Facilities/`, `features/` (Land of Invention's
`levels/science/rides/robot/`); the missions' obstacles are in
`obstacles/`, never on sale ([missions.md](missions.md#obstacles)). In it, its settings (`robot.sam`), its
model (`robot.MD2`), its script and the rest. The kind's folder has the
kind's settings too (`rides/Rides.sam`), which an object's own override
([sam.md](../formats/sam.md)):

| Setting | What |
|---|---|
| `Info.Id` | Its number ([tpws.md](../formats/tpws.md#the-objects)) |
| `Info.Name` | A name of the makers': "Giant Robot Swing", "bellows" |
| `Info.Shape` | Its footprint, a grid ([sam.md](../formats/sam.md)): `*` its own, `.` free, `2` its entrance, `N` its exit, and others its own (`S`, `<`, `>`) |
| `Info.HasQueue` | Whether visitors queue for it: 1 for rides, 0 for the rest |
| `Upgrades[0].CostOfUpgrade` | Its price: the robot's 1,400 |
| `Research.Group` | When it's to be had: 0 at the start, 1 to 5 by research |
| `UsageInfo.ExcitementLevel`, `Upgrades[0].InitCapacity`, `Info.WorkingLife` | Its ratings, as the window shows them |

What's on sale at the start is what has `Research.Group` 0 and a price:
in Land of Invention, seven rides, two sideshows, three shops, six
facilities and six features. The rides at § 0 (the Log Mill, the
waterwheel, the tour ride, the changing hut) are the special rides the
park's areas hold, not for sale. These are the recording's lists, item
for item and price for price, its rides (Bellows Bouncer § 1,000,
Dizzy Droid § 1,400 and on), sideshows (Grab-a-Prize § 500, Test Of
Strength § 700) and shops (Balloon Shop § 700, Drinks Shop § 500,
Fries Shop § 500) *(confirmed)*.

### Their names

The names shown aren't the settings': they're pairs of
`OBJECT_NAMES.str`, a first word and the rest (strings 22 and 23,
"Dizzy", "Droid"). Which pair is an object's isn't in its settings.
`OLD_OBJECT_NAMES.str` holds, at the same places, older names, most of
them the settings' own ("Giant Robot Swing" at pair 11, "Mechanical
Crane" at 3, Grab-a-Prize's), so an object's name is the pair whose old
name is its own, whole or by its first word ("bellows", Bellows
Bouncer) *(inferred)*. Not all match: Land of Invention's "TV sim",
shown as "InvenTiVe World", matches the Polar Zone's old "TV Sim"
("Arctic Adventure"); its log flume ("Splashomatic") and its shops
match none, and keep their settings' names.

### The Buy window

At 640 × 480, as the recording shows it at 2:43; its strings `UITEXT`'s:

| Piece | Model | Where |
|---|---|---|
| The window | `!window_s` | (62, 52), 566 × 366; its title, "Buy Ride", "Buy Sideshow", "Buy Shop", "Buy Facility", "Buy Feature" (271 to 275), in `TITLEMED` at (100, 59) |
| The tabs | `tab_arides`, `tab_asshows`, `tab_ashops`, `tab_afacility`, `tab_afeatures`: the shown one `down` | from (102, 84), 42 × 48 each, side by side |
| The panel | `!f_plain` | (82, 132), 306 × 262 |
| "Item", "Price" (276, 277) | `!cbut` | (103, 150), 131 × 35; (237, 150), 65 × 35 |
| The list | `!frame`, its middle black | (98, 186), 251 × 197; a row 13 pixels from y 188, in `GAME7AA`, the price right at x 300; the chosen row grey-blue; a tick (`gtick`) by those the park has |
| Its picture | `!frame` | (442, 92), 126 square: its model turning, its name over it |
| Its ratings | `!frame` | (403, 232), 205 × 123: "Number Already Owned" ("None", 279, 284), "Excitement rating" (280) as a bar, "Safe capacity" (281), "Reliability" (282) and "Working life" (283) as bars, its description below |
| Buy, cancel | `b_okay`, `mb_cancel` | (542, 366); (580, 369) |

A bar is `ridestatbar`'s picture: its top half red through yellow to
green, filled to the rating, its bottom half grey beyond. The list is
by name; coasters have a "- Ready-made versions" line under them (16).
The rides' ratings Reluva has are their settings' excitement and
working life, out of 100, and their capacity; the recording's Bellows Bouncer's bars
are about as long *(guessed)*.

### Placing

"Rotate using < and >, click to build. Hold CTRL when placing to build
multiple" (`UIHELPTEXT` 146). The footprint is the shape's cells, laid
from its first cell, the first of its last row, the rows going y + 1 on
from it, the columns x + 1: the prebuilt park's robot is so
([tpws.md](../formats/tpws.md#the-objects)) *(confirmed)*. Turned, it
turns about that cell as its model does, east towards south. Its price,
under the money while it's placed, is half the money's size, in yellow:
the recording's "§ 500" is half as wide as "§ 500" in the money's font,
and no font of the game's is that size *(guessed)*.

### Running

Once placed, an attraction runs by its own script beside its model
(`robot.RSE`, [scripts.md](scripts.md)): it builds itself, its animation
0 (the Bellows Bouncer rising out of the ground over 3 seconds, the
robot over 5), then plays what it plays while it waits for riders, a
loop of its own (the robot's swing rocking, the fountain's top turning),
as its scripts run without guests *(inferred)*. With riders it runs
its rides' cycles, taking the park's visitors from its queue
([visitors.md](visitors.md#riding)), and shows them in its seats or
bouncing on it ([visitors.md](visitors.md#on-the-ride)). Its sign shows
its name, its pair of `OBJECT_NAMES` in two lines, lettered over its
`SIGN1`, `SIGN2` textures as the `.sgn` file beside it has it: a font of
the game's, sizes and colours ([sgn.md](../formats/sgn.md)).

### Its window and menu

Clicking an attraction opens its window, right-clicking it its menu, as
the recording does at 11:16 to 11:28.

The menu (at 640 × 480, the menus' see-through panel at the point
clicked, the attraction's name over its items): "Call engineer",
"Cancel called engineer", "Build/Edit line", "Buy upgrade", "Close",
"Delete (scrap value: $500)", "Ride", "View financial summary", "Edit
name", "Buy another (cost: $1000)", "View opinion summary" (`UITEXT` 524
to 535). Reluva's has all but the opinion summary:

- **Buy upgrade**, or cancel it while it's being made (127)
  ([research.md](research.md#upgrades)).
- **Ride** moves the camera close in over it; how the game's view from
  a ride is placed isn't worked out.
- **View financial summary**: "Financial Statistics - {4}" (45), the
  users, takings, running cost, cost of goods and profit last month,
  and the profit to date (170 to 178), in a window laid out by guess.
- **Edit name**: typed into a window of its own, Return or the tick to
  keep it, Backspace to take a character off; its windows and menu then
  show it. Its window is laid out by guess.
- **Buy another** starts placing another at its price, turned as it is.
- **Delete** sells it for its scrap value: its cells bare, its queue
  gone, those in it or on it back to the path.

A shop's and sideshow's menu has its own open, close, summary and name
(547 to 550); a plant's, call a gardener (583, 584); each, buy another
and delete.

The window (`!window_s`, (90, 62), 515 × 296; the name at (118, 69)):
its model turning in a frame at (108, 104), "Not Upgraded" (37) or
"Upgraded to level {3}" (38) under it;
its ratings and accounts in a frame at (242, 104), a line every 16
pixels, values at x 372: "Users last month", "Excitement rating",
"Customer satisfaction", "Reliability", "State of repair", "Remaining
life" (bars), "Monthly running cost", "Scrap value" (117 to 124); a
shop's and sideshow's their own (128 to 143); the weather icons under
the model; sliders for speed, capacity and duration at the right; and
its price in a box at (286, 250): − and + about it, "Entry Price"
(428), a shop's "Sale price", a sideshow's "Price of game"; the door, open
or shut, at the bottom left; the tick. Reluva draws the model, the ratings
it has (users last month, excitement, reliability as 1 less its
`WearRate` tenths *(guessed)*, the state of repair, the running cost, the
scrap value), the capacity, the price, which − and + change by § 1, and
the door, which opens and closes it, and its level by the capacity;
not the sliders, customer satisfaction, remaining life or the weather
icons.

What it brings in, a month's users and takings; what it costs, its
level's `RunningCost` paid each month, its upgrades, and its
`UsageInfo.InitCostOfGoods` paid for each sale *(guessed)*; its profit
to date; and its scrap value, its price by
`Upgrades[0].ScrapValueYear1` to `4` percent as it ages a year, are
kept.

### Wear and breakdowns

Each rider wears a ride by a fifth of its `Upgrades[0].WearRate` ("out
of 10", its comment says) percent of repair *(guessed)*. At 0 it's
broken: none get on, and its script is told so in `VAR_BREAKSTAT`,
which the scripts test before each cycle, playing their breakdown (the
robot's branch at `a333`). Below `RideBreakDown.Imminent` (10), or
broken, an engineer is called ([staff.md](staff.md#their-work)), who
mends it whole. Closed, none get on either, and its script is told in
`VAR_RIDECLOSED`.

## In Reluva

`Attractions` (`src/engines/tpw/park/attractions.cpp`) reads a world's
attractions as above. `BuyWindow` (`src/engines/tpw/ui/buy_window.cpp`)
draws the window and takes its clicks; the Attractions menu's Build
Attraction, or a right click on its button, opens it. Its tick starts
placing: the footprint follows the pointer, blue where all its cells
are bare land and the money's there, red where not; `<` and `>` (or
`,` and `.`) turn it; a click places it (`Park::place`), its price
paid, its cells taken, its model drawn; a ride's queue is then laid
from its entrance, as the path tool lays one. A right click or Esc
drops it. `ParkScreen` runs each placed object's script on the park's
clock (`RideRun`, [script-instructions.md](script-instructions.md#in-reluva),
without its guests), and draws its model posed by what it plays; those
standing as the park opens are run 20 seconds on at once, past their
building *(guessed)*.

`reluva-tool tpw buy-shot <game folder> <world folder> <out.png> [tab
[row]]` draws the window over a new park and lists what's on sale;
`park-run` buys the Bellows Bouncer, places it and lays its queue to
the path. `window-shot ... summary` and `rename` draw the financial summary and
naming windows. The unit tests (`tpw.unit.park`, `tpw.unit.ui`) read a
world's attractions built in the test, place, rename and delete them,
and click the windows.

## Sources and checks

- **Where the facts come from:** the recording, 2:36 to 2:55; Land of
  Invention's settings; the string tables; the interface's models.
- **How it was checked:** `buy-shot` beside the recording at 2:43: its
  lists and prices the recording's, its pieces within a few pixels;
  the placed ride's footprint the prebuilt park's.

## Open questions

- Each object's name: where the game finds its pair of `OBJECT_NAMES`;
  its description (`OBJECT_DESC.str`), which Reluva doesn't show.
- What "Reliability" is made from; the weather icons under the ratings
  (`clim_*`, the settings' `WeatherModifier`); the two small header
  buttons; the tick's green (Reluva's comes out yellow).
- Whether the game's footprint shows the model as well; Ctrl to place
  several; what placing does to the ground (the hoardings, the
  particle effect on building, `Info.CreateParticleEffect`).
- Exits: where visitors leave, and the path they need.
- How wear comes, by rider or by cycle; `RideBreakDown.WarnPlayer`;
  the sliders; the opinion summary; the game's view from a ride; the
  financial summary's and naming windows' looks.
- The signs: how close Reluva's lettering is to the game's
  ([sgn.md](../formats/sgn.md#open-questions)).
