# SimCoaster: the park's tools, menus and windows of information

> **Status:** partial · **Code:** `src/engines/tpw/ui/info_windows.cpp`, `src/engines/tpw/ui/park_screen.cpp` · **Checked against:** the recording · **Confidence:** inferred where the recording shows it, guessed elsewhere

## Overview

Besides building and staff, the HUD's tools and menus give the player
the park's information: Visitor Statistics, each visitor's window, the
lists of visitors and attractions, the park's statistics and awards,
its map, and the Message Center, where the park's letters arrive. Esc
brings up the game's menu. The recording opens Visitor Statistics at
7:20, 10:00, 21:40 and 22:30, a visitor's window at 9:30 and 16:10, the
Message Center at 20:10 and 24:10, the game's menu at 4:20, and looks
from the ground at 11:50, 20:40 and 23:10.

## How it works

### The tools

Down the HUD's left (`UIHELPTEXT` 116 to 120):

- **Selection mode**, the pointer: clicks open windows and menus.
- **Path mode** lays path ([paths.md](paths.md)).
- **Delete mode**, the bulldozer: a click takes up an attraction, sold
  for its scrap value as its menu's Delete does, or a cell of path, for
  nothing back *(guessed)*; not an obstacle.
- **Camcorder**: a click looks from the ground there, at a visitor's eye
  height, as the recording's camera does at 11:50 *(inferred)*; the
  view turns and moves as ever, and Home brings the usual one back.
- **Send email postcard** went through EA's servers, now gone
  ([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)); it does nothing.

### The menus

| Menu item | Window |
|---|---|
| Attractions: List All Attractions (835) | a tab a kind (`tab_arides` and the rest; "All Rides" to "All Facilities", 342 to 346), each kind's columns: a ride's name, users last month, excitement and state of repair (347 to 350); a shop's or facility's customers, profit last month and to date (351 to 354); a sideshow's likewise (356 to 360); a feature's number owned and health, its water (362 to 364). A row and the tick open its window |
| Visitors: Visitor Statistics (840) | below |
| Visitors: List All Visitors (838) | a tab a generation (`tab_vall`, `tab_vchild`, `tab_vadult`, `tab_vsenior`); each visitor's number, cash, days in the park, rides ridden, thought and happiness (366 to 371); a row and the tick open their window |
| Visitors: Locate Visitors (839) | the camera to each visitor in the park in turn |
| Park: Park Statistics (846) | the visitors in the park, the arrival rate, the park's rating and the visitors to date (423 to 426): the rate as the visitors the park draws now, the rating as its points *(guessed)* |
| Park: Park Awards (850) | "Park Awards - {20}", the world's name, and the eight awards (453 to 460), each held with its measure, else "None" ([park-awards.md](park-awards.md)) |
| Maps: Park Map (852) | below |
| The envelope | the Message Center (861), below |

The Attractions' and Visitors' lists, Park Statistics and Awards are
laid out by guess in the staff list's style. Challenges (849) opens the
objectives' window on its Challenges tab ([challenges.md](challenges.md)).
Overview Map (853) and Visitor Opinion (854) do nothing yet.

### Visitor Statistics

At 640 × 480, as the recording shows it at 7:20: the window (175, 8),
345 × 392, "Visitor Statistics" (376); the tabs of `tab_vall` to
`tab_vsenior` from (228, 46), 44 apart; a panel at (207, 88) headed "All
Visitors", "Children", "Adults" or "Senior Citizens" (40 to 43). In a
frame at (228, 128): "Average Visitor Happiness" as a bar from x 360,
"Average Rides Ridden", "Average Sideshows Played", "Average Number Of
Purchases" (377 to 381); "Most Dominant Thoughts" (382) under a plate of
their three bubbles, which Reluva writes as words. In a frame at (228,
302): "Visitors Currently In Park", "Total Visitors To Date" (383, 384),
and the shares "Happy :", "Undecided :", "Unhappy :" (1 to 3) as yellow,
orange and blue bars, happy from 66 percent happiness and unhappy below
33 *(guessed)*. The red cross at (480, 362).

### A visitor's window

At 9:30: "Visitor #{28}" (658) at (145, 95), 405 × 232; their picture in
a frame at (165, 138), which Reluva leaves empty; in a frame at (268,
138), a line every 16 pixels, "Preferred excitement rating" and
"Happiness" as bars, "Rides ridden", "Purchases made", "Sideshows played",
"Sideshows won" and "Current status" (207, 205, 201, 204, 202, 203, 206),
the status in `KIDSTATES.str` ("Doing nothing", "Lining-up", "Using:
{0}"). Below: a button that takes the camera to them (Reluva's is
`b_goto`), the arrows to the visitors before and after them, their
thought by its bubble, and the red cross. Clicking a visitor opens it.
Sideshows won stay at 0: their prizes aren't played for.

### The Message Center

At 20:10: the window (65, 12), 560 × 388, "Message Center" (861);
"Inbox" and "Details" (860, 859); "Subject" and "Received" (209, 210)
over the list in a frame at (88, 128), newest first, each subject and
the date it came; in a frame at (325, 82), "From:", "To:" and "Subject:"
(856, 855, 857) in yellow, the sender, the player and the subject, then
the message. `b_delete` at the bottom left deletes the one chosen; the
tick closes it. The recording's "From" is "The Adviser" for the
park's own news; Reluva signs the challenges' letters from their
directors, the rest from the president.

Every message the park writes ([finance.md](finance.md),
[missions.md](missions.md)) goes there, unread, counted on the
envelope's button, as the recording's "1" at its foot; opening the
Message Center reads them all. The president's letters on an objective
done and the game's end show over the park as well *(guessed)*.

### The Park Map

The world's map picture (`levels/science/2dmap.tga`, 512 × 512), in a
frame at (165, 86), 310 across *(guessed)*; on it, each attraction's mark
(`2dmap/map_ride.tga` and the others of its kind), the visitors as white
dots and the camera's place as a red cross; a click on it takes the
camera there. The picture covers 96 cells across, the park's cell (0,
0) at its left and 5.6 cells from its top, as Land of Invention's
entrance (47, 17), fence (y 48) and river (x 60) lie on it *(inferred)*.

### The game's menu

Esc, at 4:20: the park darkened, and in `MENUBIG`, centred, 46 pixels
apart from y 66, cyan and white under the mouse: "Load Game", "Save
Game", "Select A New Player", "Go Online", "Options", "Go to Coaster
Design Kit", "Resume Game", "Quit Game" (768, 769, 774, 766, 770, 50, 771,
772). The park's clock stops meanwhile. Only "Resume Game" (or Esc) and
"Quit Game" do anything yet; the others are grey.

## In Reluva

The windows are in `src/engines/tpw/ui/info_windows.cpp`, drawn and
fed events by `ParkScreen` and `playPark`. `reluva-tool tpw window-shot
<game folder> <world folder> <out.png>
<visitors|visitor|visitorlist|attractions|parkstats|awards|messages|gamemenu|map>`
draws them; `messages` runs the park to its first annual report.
`park-run` quits by the game's menu. The unit tests (`tpw.unit.ui`,
`tpw.unit.park`) click the game's menu, the Message Center, the map and
the visitors' windows, and take up path.

## Sources and checks

- **Where the facts come from:** the recording's frames named above;
  `UITEXT.str`, `UIHELPTEXT.str`, `KIDSTATES.str`, `THOUGHTS.str`; the
  interface's models; the worlds' map pictures.
- **How it was checked:** `window-shot` set beside the recording's frames
  at 7:22 (Visitor Statistics), 9:30 (a visitor), 20:10 (the Message
  Center) and 4:20 (the game's menu): their pieces within a few pixels.
  The map's fit to the park's cells was judged by eye.

## Open questions

- The other windows' looks; the visitors' and the thoughts' pictures in
  them; who signs which message.
- When a message shows over the park, rather than only arriving.
- How the arrival rate and the park's rating are reckoned.
- What the bulldozer refunds for path, and what it may take up.
- The camcorder's view, beyond being near the ground.
- The Overview Map and Visitor Opinion.
