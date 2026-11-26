# SimCoaster: the park

> **Status:** partial · **Code:** `src/engines/tpw/park/park.cpp` · **Checked against:** a recording, and the four worlds' files · **Confidence:** inferred unless marked

## Overview

After a world is chosen on the globe ([globe.md](globe.md)) and its
loading screen ([loading-screen.md](loading-screen.md)), the game
flies the camera along the road to the park's gates, shows the
mission's briefing ("Current Objective") and the park runs: from 1:34
in the recording ([research/footage.md](../research/footage.md)). A new
park is the world's own land and buildings, with what its settings and
its mission give it. Reluva sets one up and shows it running, with its
money and its date, and a camera that moves over it; building, guests
and the rest are still to come ([PLAN.md](../PLAN.md#phases)).

## How it works

### The park's cells

The park is laid out in cells 10 units square: cell (x, y) of the
settings is the world's square from (10x, 10y) to (10x + 10, 10y + 10)
along its x and z, on the ground at height 0 *(inferred)*. The world's
attribute map ([attribute-map.md](../formats/attribute-map.md)) has a
tile for each cell, at row x and column y, and set so it matches what
the settings say is there: the entrance's two cells (47, 17) and (48,
17) are its path's tiles; the ticket booths' cells (45, 8) and (45, 11)
its booths'. Drawn over the world, the booths' cells fall on the booths
in the world's model, the bus stops' by its shelters, the entrance in
the gap of the park's wall; and the attribute map's river tiles fall
where the world's river emitters are ([omp.md](../formats/omp.md)),
x 610 to 950 and z 130 to 460 in Land of Invention. The ground's cells
([lnd.md](../formats/lnd.md)) are a quarter of a park cell, 2.5 units.

### What a new park has

- **The world's fixed items** (`levels/<world>/Standard.sam`,
  `FixedItemInfo`): two ticket booths (`TicketBooth[0]` and `[1]`, with
  an angle of 512, a half turn of 1,024 perhaps *(guessed)*), the
  entrance's two cells, three bus stops, the crossings' cells on the
  bus's side and the park's, and a strike area. The entrance's and the first booth's cells are the
  same in all three playable worlds. All but the gates are the world's own model's
  (`terrain/base.MD2`); the gates are the world's `features/Gates`
  object, `ParkName.GateObjectId` 1601 in its `global.sam`, one of the
  fixed items the prebuilt park lists unplaced, with the bunting and the
  booths' building, drawn from the world's corner
  ([fly-in.md](fly-in.md#the-fixed-items)).
- **The mission's** (`missions/missNN.sam`; Operation Expand!, the first,
  is `miss00`): its objective (`ExitStatistic[0]`: `Type` 8 with
  `TargetValue` 40, "Get 40 people in the park at once", as its briefing
  says), the obstacle it clears (`Obstacle.Type` 1700), and the park's
  fence once it's done, `BoundaryClicks`, 43 corners in the first
  mission, counted from by the entrance ([land.md](land.md)).
- **Its land**: the world's `hoardings.map`, grown by the missions done
  in this world ([land.md](land.md)).
- **The prebuilt park's path, queue and objects**, in Land of
  Invention, from the game's own saved park
  ([paths.md](paths.md#where-path-can-go), [tpws.md](../formats/tpws.md)):
  76 cells of path from the entrance; the Giant Robot Swing beside it
  with its queue of 5; the Electric Fountain, three rocks, the mission's
  fence and other obstacles, and four rides in the park's further areas,
  each its world's model, laid from its first cell and turned by its
  angle, and run by its script ([building.md](building.md#running)).
- **Its money**: `BankAccountInfo.InitialCash` 50000 of the general
  settings (`levels/Standard.sam`), and what each visitor pays to come
  in, `InitialAdmissionFee` 25. The recording's money is § 50025 as the
  briefing closes: the first visitor's fee, it seems *(inferred)*.

### Time

The park opens on 1 January 2000; the HUD's date reads "14.Jan.00" at
2:30 in the recording, 19 Jan at 2:50, 21 Jan at 3:00, 26 Jan at 3:20,
30 Jan at 3:35: a day every 4 seconds *(inferred)*, the first day back
at about 1:38, as the camera flies in. Over the whole recording the
days come slower, 4.6 seconds each (19 October at 24:00): the game
stops its clock, it seems, while some windows are open. Reluva stops
it while the briefing is open ([briefing.md](briefing.md)).

### Visitors

The general settings say how visitors come (`Arrival`): `MinPeople` 1,
`TimeBetweenArrivals` 25, `FixedRate` 15, `NewParkBonus` 20,
`PointsPerVisitor` 25, and how rain, storms, snow and heat change it
(90, 80, 75 and 120 percent). How the game uses them isn't known.
Reluva reads them so *(guessed)*: a park draws as many visitors as its
points pay for, at `PointsPerVisitor` a visitor and never fewer than
`MinPeople`, its points `FixedRate` and `NewParkBonus` and each of its
attractions' `Info.AttractionValue` (rides 25 by their kind's settings,
the robot 15, features 10), scaled by the weather
([weather.md](weather.md)); while it has fewer, one comes every
`TimeBetweenArrivals` seconds. Land of Invention's new park, with the
prebuilt park's attractions, has 330 points and draws 13. The
recording's first visitor paid by 2:32, under a minute in; it had 6 at
5:40 and 10 at 7:20, as one every 25 seconds would give, but 34 at
9:56, faster than that: how a park's points grow with its building, or
how many come at once, isn't known.

The recording's visitors in the park, from the Visitor Statistics
window: 6 at 5:40, 10 at 7:20, 34 at 9:56, 64 at 21:40, 67 at 22:25,
as the player built. They come by bus, it seems, to the stops on the
road; through the ticket booths, where they pay the admission
(`InitialAdmissionFee` 25); and in by the entrance. Reluva walks each
along that way *(guessed)*, cell to cell, a child at 12 units a second
*(guessed)* and the others slower, at their type's `SpeedMultiplier`
of the general settings: off the
bus at the first stop, over the crossing from the bus's side to the
park's, past the first booth, paying, through the entrance's first cell
and up the path four cells in, the attribute map's path tiles
([attribute-map.md](../formats/attribute-map.md)). Once in, they walk
the paths and ride the rides ([visitors.md](visitors.md)).

The settings have 24 types of visitor (`PeepTypes`), 8 of each
generation (children, parents, grandparents), each with the excitement
it likes, its money, how soon it's bored and its pace: the children's
1, the parents' 0.95 down to 0.81, the grandparents' 0.47 down to 0.33.
Reluva gives each visitor a type, and draws them by one of their
generation's sprites ([sprites.md](../formats/sprites.md)): four
children's, two parents', two grandparents'. They walk or stand, turned
by their way against the camera's, about 5 units tall, half a path's
cell, as the recording's.

### The HUD

The screen's furniture is the interface's models
([ui-models.md](../formats/ui-models.md#the-parks-models)), placed as
the recording has them at 2:38 and 5:00 at 640 × 480, each model's
picture matched against its frames:

| Piece | Model | Where |
|---|---|---|
| The thermometer of the park's mood | `happyguage` | (14, 5), 36 × 96 |
| Its face | `face`: six, unhappiest first, the last a red no-entry sign | (17, 69), 30 × 30 |
| The money | in `CASHMED`, white with a black shadow 2 pixels down and right; "$ 50025", the font's "$" drawn like a "§", no separators | its first digit at (108, 13) |
| The compass | `comp`, a clover-shaped plate; `comp_rotatel`, `comp_rotater` (above), `comp_zoomin`, `comp_zoomout` (below) in its corners; the dial `compgizn` over its middle | (553, 7), 80 square; the buttons 40 square; the dial (569, 23), 48 |
| The date | `datebox`; the date in `DATESMALL`, "30.Jan.00", the month's name from `MONTHS.str` | (554, 89), 80 × 32 |
| The tools down the left | `toolbox`, a bar; on it `tb_pointer`, `tb_path` (a hand), `tb_delete` (a bulldozer), `tb_camera`, `tb_postcard` (a newspaper) | the bar (7, 249), 48 × 218; the tools at x 12, y 253, 296, 339, 381 and 424, 38 square |
| The buttons along the bottom | `gizmo`, a bar; `giz_message` (an envelope, the messages' count in a black ball), then on the bar `giz_attract`, `giz_staff`, `giz_visitors`, `giz_finance`, `giz_park` (the gate), `giz_maps` | the bar (163, 436), 360 × 13; the buttons at y 425, x 107, 181, 237, 293, 349, 405 and 461, 48 square |
| Help | `b_help`, larger than its model | (570, 414), 36 square, its size by eye *(guessed)* |

A button shows its states as the models name them: the tool chosen as
`down` (the pointer, yellow, at first), a button under the mouse
`hilite`, the zoom-out button `disabled`, greyed, with the camera as far
out as it goes (2:38).

### Menus and tooltips

A click on one of the bottom buttons opens its menu over it: a panel
(`!f_plain`) 147 pixels wide, its left 49 pixels left of the button, its
foot at y 416, joined to the button by a tail (`gizmojoin`); 38 pixels
for its title, in `TITLEMED`, and 18 for each item, in `GAME7AA`, 10 in.
It rises from the bar over about 0.3 seconds, slowing as it comes, then
shows its items: grey-blue, white under the mouse, dark where they can't
be chosen (Research Lab, before a park can research). The menus, as the
recording opens them (Attractions 4:02, Visitors 9:49, Staff 11:05, Park 11:47) and their strings in
`UITEXT`:

| Button | Menu |
|---|---|
| Attractions | "Attractions": Build Attraction, List All Attractions, Research Lab, Attraction Finance Controls (832, 833, 835, 834, 836) |
| Staff | "Staff": Hire Staff, List All Staff, Train Staff, Locate Staff, Staff Finance Controls (826 to 831) |
| Visitors | "Visitors": Visitor Statistics, List All Visitors, Locate Visitors (837, 840, 838, 839) |
| Park | "Park": Park Gates, Park Awards, Park Statistics, Objectives, Challenges, Close park (845, 847, 850, 846, 848, 849, 600; "Open park" 599) |
| Finance | "Finance": Financial Statistics, Loans, Share Portfolio (841 to 844), in the table's order, never opened in the recording *(guessed)* |
| Maps | "Maps": Park Map, Overview Map, Visitor Opinion (851 to 854), likewise *(guessed)* |

The messages' button has none: it's the Message Center's (`UITEXT`
861). Park's Objectives shows the mission's briefing again
([briefing.md](briefing.md)), as at 11:47.

A button the mouse rests on shows a tooltip: a pale yellow box
(`!tooltip`, its textures `tooltip1` to `tooltip8` fading in) with the
button's help in `GAME8AA`, dark, from `UIHELPTEXT`: the tools' "Selection
mode", "Path mode", "Delete mode", "Camcorder", "Send email postcard"
(116 to 120); the bottom buttons' "Message center", "Attractions menu /
Right-click to Buy Attractions" and the like (167 to 173); the
compass's "Rotate left", "Rotate right", "Zoom in", "Zoom out" (112,
113, 102, 103). How long the mouse must rest, and where the box goes
against the pointer, aren't measured *(guessed)*.

The thermometer's model is full, its liquid light blue; an empty one is
black above the face. In a bare park it's empty and its face the red
no-entry sign, until the first attraction is built (the recording's
first sideshow, at 2:45): then the face turns to the middle one and the
liquid rises, orange over blue, and the face turns happier as it does
(4:40) *(inferred)*. The park's mood isn't worked out yet: Reluva's
parks are bare, and show the red sign.

## In Reluva

`Park` (`src/engines/tpw/park/park.cpp`) sets up a new park from the
general settings, the world's and the mission's, and keeps its clock;
`ParkScreen` (`src/engines/tpw/ui/park_screen.cpp`) draws its world, as the viewer draws it
([lnd.md](../formats/lnd.md#in-reluva)), through a camera that starts
on the road before the entrance, the fixed items marked (the entrance
yellow, the booths red, the bus stops blue), the money and date over
it, and its visitors; `ParkHud` (`src/engines/tpw/ui/park_hud.cpp`)
draws the HUD over it and follows the mouse over its buttons. After the
loading screen the mission's briefing shows over it
([briefing.md](briefing.md)), then `playPark` runs it: the arrow keys
move the camera, Q and E or the compass's arrows (held) turn it, Page
Up and Page Down, the compass's magnifiers or the wheel zoom, Home goes
back, Esc opens the game's menu. A tool is chosen by clicking it: the
path tool lays path ([paths.md](paths.md)); the bulldozer and the
camcorder are in [park-windows.md](park-windows.md#the-tools). The
bottom buttons open their menus; of their items Park's Objectives, Park
Gates (the entry price, and the gates opened and closed) and Close Park,
all the Attractions', Staff, Visitors', Finance and Maps menus' items,
and Challenges, do something ([staff.md](staff.md),
[research.md](research.md), [finance.md](finance.md),
[park-windows.md](park-windows.md)). The park's messages arrive in the
Message Center. Clicking the gates opens their window,
right-clicking them their menu: "View entry price details", "Open park"
or "Close park" (`UITEXT` 597, 599, 600); closed, no visitors come. The
park is set up as the player's missions done leave it, their objective
played if it's in this world ([missions.md](missions.md)).

`reluva-tool tpw park-shot <game folder> <world folder> <out.png>
[seconds] [yaw pitch distance]` draws a new park, its clock run on;
`reluva-tool tpw park-run <game folder> <world folder>` plays one
headless: its briefing turned to the details and back, Proceed, path
laid, the Park menu's Objectives chosen and the briefing proceeded
from, the camera moved by its keys and turned by the compass, the
bulldozer chosen, the Attractions menu opened and closed, until a
visitor has come in and paid (with `-shots`, the screen saved at the
times given), with a
checksum of the screens, which the game-files test checks
(`tpw.gamedata.park`). The
unit tests (`tpw.unit.park`) set a park up from settings built in the
test and check its calendar; `tpw.unit.ui` clicks the HUD's tools,
holds its compass's buttons and picks from its menus.

## Sources and checks

- **Where the facts come from:** the general settings, the worlds' and
  the missions'; the worlds' attribute maps and placed objects; the
  recording from 1:25 on, its HUD's money and dates read off its frames,
  its pieces found by matching the interface's models against them.
- **How it was checked:** `park-shot` from above Land of Invention's
  entrance: the booths', bus stops' and entrance's cells on the world's
  booths, shelters and the wall's gap.

## Open questions

- How the park's fence is drawn ([land.md](land.md)).
- The fly-in's details ([fly-in.md](fly-in.md#open-questions)).
- The HUD: the windows its menus' items open, and what a right click
  on a bottom button opens ("Right-click to Buy Attractions"); the
  Finance and Maps menus' order; the round newspaper button at the date box's right end
  (no model of `ui.wad` matched it); whether the dial turns with the
  view, and the small green arrow (`compgizn2`) at its edge; the
  thermometer's level and the faces by the park's mood; `compgiz`'s
  five meshes (the compass whole, in states), `gizmojoin`.
- How fast the clock runs at each of the game's speeds, and when it
  stops.
- How the game turns its `Arrival` settings into visitors, and the
  way they walk in; what they do once in ([visitors.md](visitors.md)).
