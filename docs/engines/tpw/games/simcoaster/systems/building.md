# SimCoaster: building attractions

> **Status:** partial · **Code:** `src/engines/tpw/park/attractions.cpp` · **Checked against:** a recording, and Land of Invention's settings · **Confidence:** inferred unless marked

## Overview

The player buys rides, sideshows, shops, facilities and features in the
Buy window, then places them in the park. In the recording: the
Attractions menu's "Build Attraction" (2:36), the "Buy Ride" window
(2:43), its sideshows' tab, "Grab-a-Prize" at § 500 chosen, and its
footprint, see-through blue, following the pointer until a click
places it (2:55), its price in yellow under the money. A ride's queue
is laid next, from its entrance ([paths.md](paths.md#queues)); a
go-kart's or water ride's track after it ([tracks.md](tracks.md)).

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

The names shown aren't the settings': they're `OBJECT_NAMES.str`'s
("Dizzy", "Droid", strings 22 and 23). Which strings are an object's
isn't in its settings. `OLD_OBJECT_NAMES.str` holds, at the same
places, older names, most of them the settings' own ("Giant Robot"
"Swing" at 22 and 23), so an object's name is the one at its old
name's place *(inferred)*:

- The strings come in runs, world by world: Land of Invention's 0 to
  157, Polar Zone's 158 to 306, the rides the worlds share (ids 4xxx)
  307 to 318, Arabian Nights' 319 to 533, then prefabricated coasters'
  old names alone. An object's world's run is looked in first, as the
  same old name comes in more than one; then the shared run, then all.
- Rides', sideshows' and shops' names are pairs, a theme and a kind,
  but the pairs don't keep step with even places, as some names are one
  string ("Info Kiosk", "Drinks Shop"). The settings' name can be the
  old pair whole ("Lobster" "Laps", Super Sailcarts), its first string
  when the second is a kind of ride or game or empty ("Log Mill"
  "Special Ride", The Thrill Mill; "TV Sim" "", Arctic Adventure), or
  its second when that names a kind of shop or game ("Rocket Man"
  "Costume Shop", Costume Shop; "North Pole" "Strength Test", Test Of
  Strength).
- Features' and facilities' names are one string each ("Small Bush",
  "Bush (small)"; "Medium rock", "Boulder").

Not all come out right. Land of Invention's "TV sim", shown in the
recording as "InvenTiVe World" (whose old name is "Mad Worlds Of
Invention"), matches only the Polar Zone's "TV Sim", Arctic Adventure;
its log flume ("Splashomatic") and a few others match none and keep
their settings' names. The recording's Fries Shop is Land of
Invention's "Chips Shop" *(confirmed)*, a name that matches nothing:
Reluva takes it for the old "Fries" "Shop", in every world. The four
loudspeakers of a world all have the settings' name "Loudspeaker",
and the tunes' names ("Loudspeaker:" "Desert Nights") can't be told
apart by it: they keep it *(guessed)*.

### Their descriptions

`OBJECT_DESC.str`'s 104 strings describe the attractions and the
staff, not by any number the settings have *(inferred)*:

| Strings | What | How Reluva picks one |
|---|---|---|
| 1 to 63 | Rides, world by world: Land of Invention's to about 22, Polar Zone's to 42, Arabian Nights' to 63 | By hand, where a string's words fit one ride ("This techno kart-track", Miniature Dreadnaughts'; "The bob-sled", Iceblast's): 17 rides; the rest have none *(guessed)* |
| 64 to 69 | Sideshows | By their game, `Info.RideTypeStringIndex`: 21 an arcade gambler (64), 22 a grab-a-prize (66), 23 a race (67), 19 a shooting gallery (68), 20 a strength test (69), others 65 |
| 70 to 81 | Shops | By what the settings' name sells: costumes, drinks, hot dogs, balloons, a restaurant, an info kiosk, photos, gifts, umbrellas, candy floss, chips, ice cream |
| 82 to 92 | Facilities and features | By their settings: watered plants (82), other features (83), security cameras (84), bins (85), loudspeakers (86), toilets, boys' (87) and girls' (88), the research lab (89), the staff room (90), the training room (91), signs (92) |
| 93 to 103 | Staff and research | The staff's kinds and abilities, and research's kinds, not used here |

The Buy window shows it under the ratings, as many lines as the frame
holds.

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

Ctrl held as it's placed, another of it follows the pointer to be placed
in turn, its line and track laid later from its menu *(guessed)*. "You
can hold the Ctrl key and click on an item to quickly buy another one"
(`TOTD` 2): a Ctrl-click on an attraction, with the pointer, picks
another of it to place, as its menu's "Buy another" does, if it's on
sale.

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
the model; sliders for speed, capacity and duration at the right
("Speed : {32}", "Capacity : {34}", "Duration : {35} secs", 932, 936,
938; "Ride runs at optimum in the green zone", "Ride wears out quicker
in the red zone", `UIHELPTEXT` 274, 275); and
its price in a box at (286, 250): − and + about it, "Entry Price"
(428), a shop's "Sale price", a sideshow's "Price of game"; the door, open
or shut, at the bottom left; the tick. Reluva draws the model, the ratings
it has (users last month, excitement, reliability as 1 less its
`WearRate` tenths *(guessed)*, the state of repair, the running cost, the
scrap value), the capacity, the price, which − and + change by § 1, and
the door, which opens and closes it, and its level; not customer
satisfaction, remaining life or the weather icons.

A ride's sliders, laid out by guess, each green to the level's red line
and red past it, the value marked white: speed from `UsageInfo.MinSpeed`
to `MaxSpeed` (1 to 100, percent), capacity from `MinCapacity` to
`MaxCapacity`, duration from `MinDuration` to `MaxDuration`, each
starting at its level's `InitSpeed`, `InitCapacity`, `InitDuration`,
its red line `RedLineSpeed`, `RedLineCapacity`, `RedLineDuration`
(`rides/Rides.sam`'s: 60, 75, 90 percent by level; 7, 8, 9 seconds). A
click on one sets it. Its capacity and duration are its script's
`VAR_CAPACITY` and `VAR_DURATION`; its excitement rating is its
`ExcitementLevel` by its speed against its first level's; past any red
line it wears twice as fast *(guessed)*.

### Photo kiosks

"Make the most of popular rides by building Photo Kiosks for them"
(`TOTD` 24, 54). The world's `upgrades/` folder holds rides' add-ons,
the go-karts' jumps and tunnels and a mine drop for tracks
(`AddOn.UpgradeType` 1), and a photo booth (`fotobth`, 1500, 2500,
3500: "Bellow Photo", "Photo Booth" in the settings, "Photo Kiosk" in
`OBJECT_NAMES` and the window's title "Photo Kiosk - {4}", `UITEXT` 44):
`Info.IsPhotoBooth` 1, "A shop" (`WhichUIType` 1), `ShopType` 7, a
photo § 40 (its cost § 12, happiness + 15), § 400 to build,
`AddOn.UpgradesAnyRide` 1, `UpgradeDistance` "4 - 10 cells distance
from main object". The recording's Buy window doesn't list it.

Reluva buys one from a ride's "Buy upgrade", which asks "Upgrade Ride"
(`UITEXT` 10): its next level ("Upgrade to level {3}", 126, or "This ride
is fully upgraded", 125) or "Photo Kiosk - {4}" with the ride's name
*(guessed)*. It's placed as an attraction is, within 10 cells of a ride,
and serves the nearest; a third of the riders coming off buy a photo,
if they have the money *(guessed)*. Attraction Finance Controls' "Photo
Sale Price" sets the kiosks' price ([finance.md](finance.md)). The other
add-ons aren't built.

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
`names <game folder> <world folder>` lists every attraction of a world,
its settings' name, the name shown and its description;
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

- Each object's name and description: where the game finds its strings
  of `OBJECT_NAMES` and `OBJECT_DESC` (Land of Invention's TV sim's
  shows a name no rule here finds); which rides' descriptions are whose.
- What "Reliability" is made from; the weather icons under the ratings
  (`clim_*`, the settings' `WeatherModifier`); the two small header
  buttons; the tick's green (Reluva's comes out yellow).
- Whether the game's footprint shows the model as well; what Ctrl does
  to lines and tracks when placing several; what placing does to the ground (the hoardings, the
  particle effect on building, `Info.CreateParticleEffect`).
- Exits: where visitors leave, and the path they need.
- How wear comes, by rider or by cycle; `RideBreakDown.WarnPlayer`;
  what the sliders do to a ride's excitement and wear, and how they look;
  how photo kiosks are bought and who buys a photo; the opinion summary; the game's view from a ride; the
  financial summary's and naming windows' looks.
- The signs: how close Reluva's lettering is to the game's
  ([sgn.md](../formats/sgn.md#open-questions)).
