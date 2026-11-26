# Star Trek: Away Team: unconfirmed items

What Reluva does that hasn't been checked against the running original.

<!-- Each item: what Reluva does, what it rests on, and what would settle
it (a recording, a screenshot, a trace), with a link to its page. -->

## Read from the code, not seen running

- **No marks on the team bar** for an enhancement or a charged item: the
  program loads `Inventory Enhanced` and the items' `Active HUD
  Sequence` but nothing found draws them; seeing the original with an
  enhanced officer would settle it
  ([systems/items.md](systems/items.md#enhancement-and-overload)).

## Seen running, not traced in the code

None now.

## Inferred

- **The Psychic Rack:** walking into range, the enemy chosen in
  Slovaak's place, its orders, and its going back on patrol
  ([systems/items.md](systems/items.md#in-reluva)).
- **Particle systems:** a disk's ring point following the cone, the particles drawn
  over the models, a slow bounce
  stopping ([systems/particles.md](systems/particles.md#open-questions)).
- **Mines' sequences:** a mine's Deploy Sequence once as it's put down,
  and its Activate held at its last frame once set off
  ([systems/items.md](systems/items.md#in-reluva)).
- **Overload:** a thrown overloaded phaser keeping its countdown, and its
  going off hurting by its own damage, its holder too
  ([systems/items.md](systems/items.md#in-reluva)).
- **Network games:** only the host starting, the chat's lines' form,
  the chosen game's and mission's font, the host starting on the first
  mission, the countdown stopped by another mission or a player leaving
  ([systems/network.md](systems/network.md#open-questions)).
- **The menus:** the credits' underlines blended by their alpha, the
  toolkit's brightness scaling colours smoothly outside the credits and
  the buttons, the slider's texture tiled, the sliders' focus, the
  keyboard screen's list of changes, voiceovers' and movies'
  volume following the effects', the
  buttons' fades counted in seconds, and the main menu's focus starting
  on START NEW GAME
  ([systems/menus.md](systems/menus.md#open-questions)).
- **Maps:** the camera's and zoom's frames taken as 30 a second, the
  drawing order's ties, the water's clock stopping while
  paused, enemies not seeing past what stands between; the path
  finding by A*, the mouse's buttons, the info bar rising under the
  mouse, the move button's order, the
  view held
  by the camera's scripts, a line's time and the beaming in's look; the
  triggers' and the enemies' rules, read from the maps' fields; choosing
  by box and Shift; fighting:
  sight blocked by the models' `Bounds Poly`, a shot meeting those on its
  line at once, stunning, the health bar's cut, and a
  click on an enemy to attack it; noise: going to look, and the rings'
  look; the enemies pursuing, retreating,
  holding the place and acting on their messages, and officers stunned
  ([systems/maps.md](systems/maps.md#open-questions)).
- **Objectives:** the windows' places and the game held still under
  them, and CONTINUE going to the next mission's briefing
  ([systems/objectives.md](systems/objectives.md#open-questions)).
- **Items:** the counts for -1, the keys matched to items by name,
  walking into range before using one, what a grenade's explosion's
  hurt falls from, healing up to the whole, and where the bar
  shows counts; the items' panels' layout and the phaser's switch on
  it, and tricorders' scans
  ([systems/items.md](systems/items.md#open-questions)).
- **The interface's buttons:** orders given with time stopped, what
  Cancel Orders drops, how an enemy's sight is chosen and drawn, and the
  escape menu holding the game
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The interface's bar and minimap:** the inventory's places in the
  team selection's order, and the zoom's steps at 30 a second ([systems/maps.md](systems/maps.md#open-questions)).
- **The gadgets:** an enemy shooting a hologram down by its hit points,
  and what's put down running its model's AIs as a map's part
  ([systems/items.md](systems/items.md#open-questions)).
- **The item panels' layout:** sequences placed from the panel's middle
  ([systems/items.md](systems/items.md#an-items-panel)).
- **The enemies' states:** where one stunned by something other than
  an officer goes to look, one running away giving up with no way to
  its spot, and End Recharge's AI before its patrol
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The triggers:** which of several in a touching trigger at once it
  holds, and a collide trigger going as one comes into the part's shape
  rather than as their movement meets it
  ([systems/maps.md](systems/maps.md#open-questions)).
- **Walking round and joining:** how near a character makes a way
  through them dearer, how a
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
- **The team selection:** the attributes out of 100, the inventory's
  empty columns and lit items, Esc closing an item's description, a
  message's time in seconds, the figures' time from the game's start,
  where a place answers a click, and the officer's name over the
  transporter's own text
  ([systems/team-selection.md](systems/team-selection.md#open-questions)).
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
- **The length of a few sounds.** For 5 of 27 sampled sounds, libvorbis
  1.3.7 gives 128 samples (6 ms) more at the end than FFmpeg does
  ([research/file-survey.md](research/file-survey.md#sound)). Reluva
  keeps libvorbis's, taking the reference decoder to cut the last block
  as the game's own `vorbisfile.dll` (February 2001) did. A recording of
  one of them played by the original would settle it.

## Guessed

- **The main menu's movies and song loop,** the movies without sound
  ([systems/startup.md](systems/startup.md#open-questions)).

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
