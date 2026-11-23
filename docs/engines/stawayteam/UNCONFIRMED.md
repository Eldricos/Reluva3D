# Star Trek: Away Team: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

- **A rotation sequence's frames go a direction at a time,** each
  direction's together. Seen in the data viewer, where the models turn
  smoothly so; the game's drawing of them isn't traced
  ([formats/binary-object.md](formats/binary-object.md#behaviour)).

## Inferred

- **The Psychic Rack:** walking into range, the enemy chosen in
  Slovaak's place, its orders, and its going back on patrol
  ([systems/items.md](systems/items.md#in-reluva)).
- **Overload:** a thrown overloaded phaser keeping its countdown, and its
  going off hurting by its own damage, its holder too
  ([systems/items.md](systems/items.md#in-reluva)).
- **Network games:** only the host starting, the chat's lines' form,
  the chosen game's and mission's font, the host starting on the first
  mission, the countdown stopped by another mission or a player leaving
  ([systems/network.md](systems/network.md#open-questions)).
- **A key or a click skips a movie:** the game ends one on anything in
  its queue of input, and what goes into the queue isn't traced
  ([systems/startup.md](systems/startup.md#the-opening-movies)).
- **The menus:** the confirm dialog and the other windows in the
  middle of the screen, the credits fading to see-through, the toolkit's
  brightness scaling colours, Normal as the level to start with, the
  gamma brightening dark colours, the slider's texture tiled from the
  screen's corner, the sliders' keys and focus, the keyboard screen's
  keys and tab sizes, voiceovers' and movies'
  volume following the effects',
  `Button Action` played on a picture button's click, Esc on
  the main menu asking to quit, a button's sound on the click, the shortcut's
  line shown once Alt is pressed, a fixed-width font's cell, and the
  sounds named `.wav` being the `.ogg` files of their names
  ([systems/menus.md](systems/menus.md#open-questions)).
- **Maps:** the camera's and zoom's frames taken as 30 a second, the
  drawing order's ties, the water's clock stopping while
  paused, enemies not seeing past what stands between; the team at the spawn points
  in order, their facing, the path finding by A*, the mouse's buttons,
  the info bar rising under the mouse, a walk's pace from its frames,
  characters' facing in radians, the move button's order, the view held
  by the camera's scripts, a line's time and the beaming in's look; the
  triggers' and the enemies' rules, read from the maps' fields; choosing
  by box and Shift, and where officers sent together stand; fighting:
  sight blocked by the models' `Bounds Poly`, which weapon a character
  uses, shots that always hit, stunning, the health bar's cut, and a
  click on an enemy to attack it; noise: going to look, walls not
  stopping it, and the rings' look; the enemies pursuing, retreating,
  holding the place and acting on their messages, and officers stunned
  ([systems/maps.md](systems/maps.md#open-questions)).
- **Objectives:** when the Win Action runs, counts adding up to complete,
  the spawn points' actions run as the map starts, protect objectives
  failing as their officer falls, the windows' places and the game held
  still under them, and CONTINUE going to the next mission's briefing
  ([systems/objectives.md](systems/objectives.md#open-questions)).
- **Items:** the counts for -1, the keys matched to items by name,
  walking into range before using one, a grenade's straight flight and
  whom its explosion reaches, healing up to the whole, and where the bar
  shows counts; the items' panels' layout and the phaser's switch on
  it, mines put down and set off, and tricorders' scans
  ([systems/items.md](systems/items.md#open-questions)).
- **The interface's buttons:** orders given with time stopped, what
  Cancel Orders drops, how an enemy's sight is chosen and drawn, and the
  escape menu holding the game
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The interface's pointer, bar and minimap:** which cursor shows
  where, the inventory's places in the team selection's order and where
  its More Indicator goes, what's marked on the minimap, and the zoom's
  steps ([systems/maps.md](systems/maps.md#open-questions)).
- **The gadgets:** the band hiding its wearer from the enemies' sight,
  a grenade's flight (Gravity's units, the bounces' end), the enemies
  going after decoys, the strikes doing their 50,000 damage rather than
  stunning, and what's put down running its model's AIs as a map's part
  ([systems/items.md](systems/items.md#open-questions)).
- **The item panels' layout:** sequences placed from the panel's middle,
  pictures and words from the info bar's corner
  ([systems/items.md](systems/items.md#an-items-panel)).
- **The enemies' states:** the order a state change runs its actions
  and transitions, an enemy losing its target going to look where it saw
  them, one up from a stun looking about where it is, where one running
  away runs to, when one looks for bodies, and a recharge's health
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The triggers and doors:** an enemy setting off a touching trigger as
  an officer does, what counts as colliding with a part, and a door out
  of the way as soon as it starts opening
  ([systems/maps.md](systems/maps.md#open-questions)).
- **Walking round and joining:** how walkers keep apart and wait, how a
  character joining the team is made an officer, and rescues failing as
  their character's killed
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The beams:** where they start and end on the shooter and the one
  shot, and how long they're shown
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The score and saves:** the save screens' rows, time and game info
  ([systems/objectives.md](systems/objectives.md#open-questions),
  [systems/menus.md](systems/menus.md#open-questions)).
- **Parts' processes:** the one chosen for an officer, and their walking
  into range first
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The maps' music:** the mission's Primary Song first, then its Random
  Songs by chance, or the map's Map Song without them
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The briefing:** the voice-overs found by name under `Levels/Voice
  Overs`, the briefing music played over and over and the menu's song
  stopped meanwhile, and text broken at its line breaks
  ([systems/briefing.md](systems/briefing.md#open-questions)).
- **The team selection:** the attributes out of 100, the inventory's
  empty columns and lit items, Esc closing an item's description, a
  message's time in seconds, the figures' time from the game's start,
  where a place answers a click, and the officer's name over the
  transporter's own text
  ([systems/team-selection.md](systems/team-selection.md#open-questions)).
- **A frame drawn by its origin:** its top left at the point less its
  origin, as the sequences' bounds have them
  ([formats/binary-object.md](formats/binary-object.md#behaviour)).
- **Which fields are objects.** The game knows it from each class's
  properties; Reluva takes a field for an object when a `{` line follows
  it. The same on all of the game's files, but a file edited by hand
  could tell them apart
  ([formats/text-object.md](formats/text-object.md#behaviour)).
- **Blended pixels' alpha.** A frame's block 1 is taken as each blended
  pixel's alpha, 255 opaque, and its pixels as RGB 5:6:5: the pictures
  look right so, but the game drawing them hasn't been compared
  ([formats/frm16.md](formats/frm16.md#blended-pixels)).
- **Objects held in place: their classes.** Taken from the text files,
  which name them for 55 such properties; the other 24 are told by their
  version and size, and none of those is in the game's binary files
  ([formats/binary-object.md](formats/binary-object.md#properties)).
- **The `.RDS` scripts' rules** where the files don't show them (text
  before a label, a label twice): the game's reader isn't traced
  ([formats/rds.md](formats/rds.md#open-questions)).
- **The length of a few sounds.** For 5 of 27 sampled sounds, libvorbis
  1.3.7 gives 128 samples (6 ms) more at the end than FFmpeg does
  ([research/file-survey.md](research/file-survey.md#sound)). Reluva
  keeps libvorbis's, taking the reference decoder to cut the last block
  as the game's own `vorbisfile.dll` (February 2001) did. A recording of
  one of them played by the original would settle it.

## Guessed

- **The main menu's movies and song loop,** the movies without sound
  ([systems/startup.md](systems/startup.md#open-questions)).
- **Points' 8 bytes as two whole numbers,** not two floats, in what
  Reluva shows of an object
  ([formats/binary-object.md](formats/binary-object.md#properties)).

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
