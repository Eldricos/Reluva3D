# Star Trek: Away Team: testing

## Automated checks

### Unit tests

`tests/engines/stawayteam/unit/`, a file per topic, each a suite
`stawayteam.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `stawayteam.unit.detection` | Recognising the game: an empty folder and the program alone aren't the game; its two files, whatever they hold, are an unknown release. Importing it: from a CD's `Setup` folder, the files Reluva uses and the movies in their folders, and not the rest; from an install without movies, with a note |

| `stawayteam.unit.maps` | A map read (its size, camera, minimap, song, ground grid, models, spawn points and polygons); its ground's cells drawn, textures blended by their values and a polygon over them; the camera kept on the map; ways found along waypoints' lines, and none where there's none; an officer at their spawn point, walking where sent at the walk's pace; a camera's script read and played: panning eased, a line shown, the team beamed in, a cut that keeps its time; an officer walking into a trigger, relayed to an arrow turned on and off, once only; a guard patrolling its route, looking about, seeing an officer, alerted and attacking; officers chosen by key, Shift and box, sent together and set running; a wall hiding an officer, a guard shooting her down past it, and an officer killing it; footsteps as loud as Stealth and the move make them, a noise heard and one not, a guard going to look and back; a mission's objectives read, counted to complete, its Win Action ending it with its message, a protect objective failing as its officer falls; a guard's alert calling those in its radius, its stun gun downing an officer, a hurt guard retreating to its alarm, one sent after an officer pursuing him; an officer's items and counts, a concussion grenade stunning a guard in its reach, a grenade hurting an officer and none left after, a hypospray healing an officer; time stopped with orders given and going on, orders cancelled, the vision button's states and an enemy's sight tinted in front of it and not behind; a phaser switched to stun stunning a guard unhurt, a remote mine put down and set off, a tricorder reading a guard's description; a relocation choosing the officers in its polygon, one leaving a map and arriving on another, the mission's end shared by its maps; a water warp's waves and its rows moved with time; the ground's fractal the same each time, its corners the map's and its edges shared by the cells beside; the view scrolled by the mouse at its edge, speeding up and slowing to a stop; Zoom Out held to its most, Reset, and Zoom In let go easing back; a guard alerted by the nearer of two officers; a wall cut in two strips, a lifted crate drawn over one and under the other; a hostage taken off the team left on the map, revived by a hypospray and back on the team; an enhancement making a team-mate's weapons do a quarter more for its 60 seconds; a phaser set to overload going off at its holder after 10 seconds, and one thrown going off where it lands; the Psychic Rack refused on an immune guard, taking another over (of the team for the while, Slovaak standing still), sent after a third which goes after it in turn, its hurt shared with Slovaak up to half his health, let go, and ending as its charge runs out; the full inventory opened by a right-click on the bar, showing a place's items above it, a click on one choosing it and closing it, and closing 1.5 seconds after the mouse leaves |
| `stawayteam.unit.menus` | Fonts: a line's width with nothing between characters, lower case drawn as capitals in a font without it, control characters left out, others it hasn't got drawn as its first; the credits' lines (comments left out, blank lines kept, long ones cut); the toolkit's widgets (a picture dimmed, a picture button's label, shortcut and click, a toggle's size and click, a frame's sides, a slider's height and dragged value, a check box's size and key); the options' file (defaults, written and read back, the sound's on the mixer); the controls (the actions' defaults as shown, a key taken from another action, a held key, kept and read back, reset); buttons: the `&` left out of the label and its shortcut kept, chosen by the shortcut (not with Ctrl), by a click, and not when let go off it; fonts' lines broken at spaces and line breaks, and beside a picture; the briefing: a mission's lines read, typed out, Space showing a line whole and the next coming, END BRIEFING leaving; the debriefing: its lines read, its panels fading in one after another, CONTINUE going on after the last; the escape menu: its buttons, RESTART MISSION asked first, CONFIGURE; the team selection: a roster, its fields, items and item types and a mission read, the team to start with, START MISSION refused while an item is missing and EXIT asking; a mission's score, the items found by it (not those at -1), an officer's items as had, found and overridden; saves written, read back and listed; the save and load screens |
| `stawayteam.unit.network` | Network games over the in-memory network: a game found by broadcast, joined, a third refused; the chat both ways in one order; only the host choosing the mission and starting; the countdown seen by both, stopped by another mission or the other leaving; all of it again losing a third of what's sent; the host leaving; Choose Netgame without a network, finding a game, the name cut at 16; the Net Lobby's typed line sent with Enter, the first mission chosen, the start given once each |
| `stawayteam.unit.particles` | Particle systems: envelopes moving and ending each way; bursts at their rate, stopping after their number, turned on and off; a particle rising, falling at its Gravity's rate and bouncing, swirling at its distance; cylinder and line start shapes; on a map, a system turned on by its message, and an entity an action makes with its own system, deleted on time |
| `stawayteam.unit.formats` | Frames (raw, each kind of run-length code, blended pixels with their alpha, damaged ones), objects in binary (a sequence and its frames, a waypoint map, objects in properties, another version, bytes left over, a class the game doesn't have), objects in text (arrays with and without counts, keys more than once, spaces at values' ends, LF and CRLF, written back as they were, text that isn't an object) and `.RDS` scripts (comments, labels with spaces, paragraphs) |

Reading `DATA.dat` is the common ZIP reader's, tested in
`common.unit.archives`, with an end record signed as the game signs it.

### The basic engine test

`stawayteam.engine` (`tests/engines/stawayteam/engine.cpp`), without the
game's files: a small install of its own (the program, and a `DATA.dat`
signed as the game's with a `Rules.txt`, two pictures, a sequence, a
model, a font, a sound and a script, the menus' pictures and fonts, all
made in the test), found as an unknown release, started (its movies not
there, so the main menu, its picture and buttons placed and its song),
S showing the difficulty window and N (CANCEL) closing it, E showing the
settings, P unchecking their check box, F showing the difficulty window
over them and Esc closing it, S showing the sound's settings and Esc
closing them, K the keyboard's and Esc closing them, C closing the settings, C showing the
credits and Esc closing them, Esc asking to quit, N closing the dialog, Q (Quit's key) asking again
and Y quitting, a checksum of the screen at each; the data viewer run
on it: its pictures shown on each background, zoomed, by folder and without the
status line, then each page in turn (the sequence stepped, the model
turned, the font, the sound played, the objects), a checksum of each
screen; then the game with rules that
aren't an object, and with none, each saying what's wrong. Against its record
(`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

That the real release is recognised: its signatures need the real
files, which the tests never hold. That the real `DATA.dat` is read
whole: `reluva-tool stawayteam data-extract` on it; and its objects and
scripts: `reluva-tool stawayteam data-check`, which should find 753
objects, all written back the same, 48 scripts with 1,378 entries, the
11 other text files, and no problems; its objects in binary:
`reluva-tool stawayteam object-check`, 3,738 models, 179 sequences, 19
fonts and 33 waypoint maps, none failed; and its pictures:
`reluva-tool stawayteam frame-check`, 671 frames read and one file not
a frame. How the pictures look: the data viewer, or `reluva-tool
stawayteam frame-png`.

Network games over a real network: two copies of Reluva, on two
machines of a local network or one, one making a game with CREATE GAME
and the other finding it in Choose Netgame and joining it, then talking
in the lobby and starting.

## A playthrough by hand

1. Add the install's folder in the launcher: it's found as "Star Trek:
   Away Team".
2. Start it: the four opening movies, each skipped by a key or a click,
   with their sound; then the main menu's screen: the starry background
   moving, the title, the Reflexive logo bottom left, the startup song,
   and the six buttons, each lit under the mouse and pressed while held
   down, a sound as one is clicked; Start New Game shows the difficulty
   window, the game's level checked, a click checking the other, the
   buttons brightening under the mouse, START GAME or CANCEL closing it;
   Settings shows the settings window, the sliders following the mouse,
   Brightness lightening the screen, the check box crossed and cleared,
   RESET DEFAULTS putting them back, the values the same when the game is
   run again; CONFIGURE SOUND shows the sound window, the music getting
   quieter and louder with its slider and silent muted, the master the
   same over both, the paired buttons taking turns; CONFIGURE KEYBOARD
   shows the keyboard screen, a tab for each category, a click on an
   action and a key binding it, another action with that key losing it
   and the list of changes saying so for 10 seconds, the mouse's labels
   swapping with Reverse Mouse Buttons, RESET asking and putting the
   defaults back, the bindings the same when the game is run again;
   Credits rolls the credits in a window
   the menu shows through, fading in at the bottom and out at the top,
   headings underlined red, roles green, starting again after the last,
   RESUME closing it; Quit (or Esc, or Q) asks, No closes the dialog, Yes
   goes back to the launcher. With "Developer: first mission" at 2 (a
   build with developer keys), START GAME shows the briefing: the movie
   in its frame, the text typed out beside the portrait, Space showing it
   whole, END BRIEFING going on to the team selection, its BRIEFING back:
   Slovaak and Marcus Refelian on the shuttle, an officer clicked in the
   roster shown in full, ADD MEMBER and REMOVE MEMBER lit when they can
   be used, the required officer kept with a message, the tabs swapping
   the panels, the inventory's items by type with a name under the
   mouse and a description on a double click, a click on a required
   item choosing the next officer who has it, the band behind those
   the officer shown has pulsing dark red, the officers on the
   transporter drawn whole, body and head, and at a new game the items
   not found yet (the cloaking band, the remote mine) missing from the
   inventories, EXIT asking; START
   MISSION (or END BRIEFING at mission 1) showing the map in the view,
   the ground blended and the models in front of one another as they
   stand, scrolled by the arrows and the mouse at the view's edges, Esc
   asking before the main menu; the interface round it, the info bar
   rising under the mouse with the minimap and the view outlined on it;
   Mission 01's opening: the view panning over the crates with Data's
   lines in the dialog, then home, the team beaming in and Marcus
   Refelian's line; the team at their spawn points, the first chosen
   with a marker under them, a click elsewhere sending them walking round
   the walls, facing the way they go, R, C and W and the move button
   running, crouching and walking; the Klingons and Romulans facing their
   ways; after the opening, Data's lines teaching the team's moves, the
   guide arrows blinking where the tutorial points, triggers' lines as
   the team walks into them; a box dragged round the team choosing them
   all, sent together to the goto spots; the Romulans walking their
   rounds, and one turning on an officer who walks into its sight, not
   one behind a wall; the team chosen and an enemy clicked: them walking
   till they see it and shooting, its flashes, it falling, and an
   officer it shoots losing health on the team bar; S or the sound button
   showing the team's footsteps as rings, bigger running than crouching,
   and a listening guard coming to look; the objectives button or O
   listing the objectives, RESUME closing it; an officer lost and the
   mission failed with RESTART starting it again; Mission 01 won: the
   debriefing's panels fading in, the time taken, Marcus Refelian's log,
   CONTINUE going on to Mission 02; Mission 02's team walking through the
   door to the Security Deck, shown there, and back; an officer's slot
   chosen showing the map they're on; the mission's song playing on its
   maps, the next starting as one ends, and the map's own for missions
   without; the pause button and Enter stopping time, the indicator and
   its words; Backspace and CANCEL ORDERS stopping the chosen; V, a click
   on an enemy and Tab showing its sight; Esc and the options button
   opening the escape menu, CONFIGURE, RESTART MISSION and END GAME; the
   phaser's panel as the info bar rises, KILL clicked to STUN and the
   phaser stunning; a proximity mine put down and going off as an enemy
   comes near; a remote mine's DETONATE; the tricorder on an enemy and
   a crate, and on a Romulan with a scan of its own; the pointer at the
   edges and over the map with each item; the inventory's places, `]`
   and `[` and its buttons, the item's name under it; M holding the info
   bar up, the minimap's dots and squares; the keypad's +, - and * zooming
   while held, and easing back near the map's size; the scrolling
   speeding up and coasting to a stop;
   Ivan Verov's band switched on from its panel, fading and draining, the
   enemies not seeing it; the holographic projection drawing an enemy's
   fire; Brexen Ijoula's stasis field holding an enemy and
   picked up again; a grenade's arc and bounces; an EM pulse grenade near
   electronics; Mission 01's prisoners not shooting; an enemy that's lost
   the team going to look where it saw them; a Klingon's body found by
   another, raising the alarm; an enemy beamed out by its alarm; the
   doors of Mission 02's ship opening for the team and the crew and
   closing after them; the team walking round one another and the
   enemies; Mission 01's first scientist rescued (Computer Systems on the
   panel, the force field down, the scientist reached and beamed out, the
   count); Data joining the team in Mission 05; Worf's death failing
   Mission 08; the phasers' orange and yellow beams and the disruptors'
   green ones as they shoot, from the gun to the one shot, and the
   footsteps silent as in the original; SAVE GAME in the escape menu, a
   name typed and saved, and LOAD GAME (escape menu, main menu, End Game
   Menu) beginning that mission again from its briefing; DELETE GAME
   asking first; a mission completed and the next briefing describing an
   item the score's found (Remote Mines, at 250); Mission 03's pond
   rippling, and the ground's blending ragged against the original's;
   Mission 05's buildings with their roofs' trim and windows up on them;
   enemies called by one
   that's seen the team, running after it out of sight, and retreating to
   their alarms when hurt (Mission 08); a grenade chosen on the bar or
   by G and thrown, the blast stunning those near, a hypospray chosen by
   H and used on a hurt officer; Mission 01 won and
   CONTINUE going to Mission 02's briefing. With "Opening movies" off, the main menu's screen at once.
   With `DATA.dat` missing or damaged, it says so instead.
3. `reluva-viewer` with the game: the pictures, their colours and
   blended edges right on each background; sequences playing; a
   character's model (`Characters/Klingon.mdl16`) turning through its
   directions in each animation; the fonts' characters; sounds playing;
   an object of each kind of file.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US English retail | Linux | Detection of the install (`reluva-tool detect`); the import's plan (`reluva-tool import`) from the install, and from a CD's layout made of its files; all 6,949 files of `DATA.dat` read, their CRC-32s matching; `data-check`: all 753 objects and 48 scripts read; `frame-check`: all 671 frames read; `object-check`: all 3,969 objects in binary read; the viewer's pages on the real data, by their screens (the Klingon turning through `Idle`'s 12 directions); the opening movies and the main menu's screen, by their screens (sound not heard: no output here) |
