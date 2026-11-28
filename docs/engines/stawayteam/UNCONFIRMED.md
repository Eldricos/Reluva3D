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

- **The Psychic Rack:** the enemy chosen in Slovaak's place, and
  waiting where it stands while what it's to shoot is out of range or
  sight ([systems/items.md](systems/items.md#in-reluva)).
- **The remote mine's going:** at once on DETONATE, its Activate not
  shown, its model having no AI to play it
  ([systems/items.md](systems/items.md#in-reluva)).
- **Network games:** only the host starting, the chat's lines' form,
  the chosen game's and mission's font, the host starting on the first
  mission, the countdown stopped by another mission or a player leaving
  ([systems/network.md](systems/network.md#open-questions)).
- **Maps:** the `Trans` and `Warped` polygons not on the ground kept
  under the models; the middle mouse button, a line without a
  voice-over's time; the
  triggers' and the enemies' rules, read from the maps' fields; choosing
  by box and Shift; fighting:
  sight's line starting where the enemy stands, a shot meeting those on its
  line at once, the stun effect not drawn, and officers waiting where
  they stand while the enemy they're to shoot is out of range or sight;
  noise: an enemy's patrol carrying on where it left off after looking
  about ([systems/maps.md](systems/maps.md#open-questions)).
- **Objectives:** the debriefing's lines typed only once their panel's
  in ([systems/objectives.md](systems/objectives.md#open-questions)).
- **Items:** the counts for -1
  ([systems/items.md](systems/items.md#open-questions)).
- **The interface's buttons:** the enemies themselves not cutting the
  sight drawn ([systems/maps.md](systems/maps.md#open-questions)).
- **The triggers:** a thrown thing found after the characters in a
  touching trigger, and the part told of a stopped move the first found
  rather than the nearest
  ([systems/maps.md](systems/maps.md#open-questions)).
- **Joining:** a character joining the team losing its other AIs, and
  on the mission's other maps as away
  ([systems/maps.md](systems/maps.md#open-questions)).
- **The beams:** their way, toward the one shot as high on them as the
  muzzle ([systems/maps.md](systems/maps.md#open-questions)).
- **The score and saves:** the save screens' rows, time and game info
  ([systems/objectives.md](systems/objectives.md#open-questions),
  [systems/menus.md](systems/menus.md#open-questions)).
- **The team selection:** the inventory's empty columns and lit items,
  the figures' time from the game's start, where a place answers a
  click, and the officer's name over the transporter's own text
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

- **The music at the start:** the first mission's Random Songs after the
  startup song, taking Mission 01 for the program's table's first
  ([systems/maps.md](systems/maps.md#in-reluva)).

## Settled

<!-- Items confirmed or corrected since: the date, and how. Once the page
it belongs to says so, an item can go. -->
None yet.
