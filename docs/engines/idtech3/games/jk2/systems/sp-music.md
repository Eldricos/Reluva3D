# Jedi Outcast: single player's music

> **Status:** partial · **Code:** `src/engines/idtech3/audio/dynamic_music.cpp`, `src/engines/idtech3/audio/sound_system.cpp` · **Checked against:** `jk2sp.exe` and `jk2gamex86.dll` 1.02a's code; `ext_data/dms.dat` and the music files · **Confidence:** from the code unless marked

## Overview

Single player's levels play *dynamic music*: each level has a set of
pieces, one for exploring and one for fighting (some a boss piece too),
with short transition pieces between. The game decides which state the
music should be in; the engine changes piece only at moments the set
allows, through the transitions, fading over a second. The sets are
described in `ext_data/dms.dat`; the pieces are MP3s under
`music/<level>/`.

## How it works

### The music asked for

- **The game** sets configstring 2 to the music's name: the
  worldspawn's `music` key (0x200b0a56, "" if none), a
  `target_play_music`'s `music` when it's used (0x200b3f30), or the
  `playmusic` command. A name is a file (`music/in_game_cutscenes/
  cin_02`) or a set (`kejim_base`), and may be two words, the second
  played after the first.
- **The client game** (0x200333c0) splits it and calls its import 34,
  `S_StartBackgroundTrack(intro, loop, starting)` (0x427350), as it
  starts and whenever configstring 2 changes.
- **The engine** (0x427350): a name with a path that's a file plays as a
  plain track, its loop after it (if the loop isn't a file but a set,
  that set starts when the intro ends, 0x427e29). Otherwise it's looked
  up as a set; an empty name is the level's own name (kept as the map
  starts, 0x4371dc), which is how most levels get their music. With
  `s_allowDynamicMusic` 0, a set's explore piece plays as a plain track.
  After a cinematic ends, the music starts again from the names last
  asked for (0x405122, 0x427310).

### `dms.dat` (0x42bcb0, 0x42bd40)

Two groups, `musicfiles` and `levelmusic`, each of named groups of
`key value` lines:

- **`levelmusic`**, by level: `explore`, `action` and `boss` name pieces
  in `musicfiles`; `uses` another level's entry instead (followed at most
  10 times; the last is the folder the files are in); `useboss` takes
  another level's boss piece, from its folder; a value `placeholder` is
  no piece (`yavin_temple`'s, which plays its worldspawn file instead);
  `death` is ignored: the death piece is always
  `music/death_music.mp3`. A set needs explore and action.
- **`musicfiles`**, by piece: an `entry` group of markers (any name,
  seconds into the piece), and up to four `exit` groups, each a
  `nextfile` (its transition piece), a `nextmark` (a marker of the
  explore piece to go on from) and any number of `time...` keys, the
  seconds the piece may leave at. Explore and action pieces must have
  an entry group and an exit with a file; a boss piece's exits are
  ignored.
- Every piece and transition must exist, and every `nextmark` must be
  one of explore's markers, or the set is refused (with
  `s_debugdynamic`, the reason printed).

The tracks are numbered: explore 0, action 1, boss 2, death 3, the
action piece's exits' transitions 4 to 7 and the explore piece's 8 to
11; 13 is silence.

### The state the game asks for

The game writes configstring 907 at the end of each frame (0x20097c50):
`death` while the player is dead, `boss` or `silence` while a script
has set them (`SET_MUSIC_STATE`), else, at most once a second, `action`
while an armed enemy that's after the player (or an alert of danger)
is near enough (1024 units, or 2048 in sight), `explore` otherwise.
The engine reads it each frame (0x428290) by its start: silence,
action, boss and death (the last two only if the set has them), anything
else explore.

### Changing state (0x42d6e0, 0x4283a9)

- **Who may cut in:** death always, and nothing cuts death; boss always,
  and only silence cuts boss; action always; explore and silence not
  while a transition plays. Otherwise the request waits.
- **To action:** at once at full volume, at a random entry marker of
  the action piece, not the one taken last time (0x42d930); from
  silence, from its start, fading in.
- **To explore, from action:** at the next of action's exit times
  within 0.3 s of where it has got to (0x42d730): that exit's transition
  plays, then explore goes on from the marker the exit names (an exit
  without one goes through `action_tr0` to explore's start). From
  silence: explore from its start.
- **To silence, from explore or action:** at an exit time, through the
  transition, then silence; from anything else at once.
- **To boss:** at once, fading in; **to death:** at once, full volume.
- A piece left keeps its place: going back to it goes on from there,
  but where a start or marker is given above.

### Playing (0x427c00, 0x427f10)

Each change fades the new piece in and the old one out, linearly, over
a second; each frame the volume is smoothed, half the last and half the
new. With under two seconds of the piece left, a transition hands over
to the state it leads to (at its entry point); any other piece starts
again from its start, crossfading; death plays once. A plain track is
silent while the state is silence. `s_musicvolume` (0.25) sets the
music's volume.

## In Reluva

`findMusicSet` reads `dms.dat` and a set from it as above, its files
checked; `DynamicMusic` plays a set's tracks through the mixer as
above, each piece decoded whole; `SoundSystem::startMusic` is the
engine's `S_StartBackgroundTrack`, a file or a set. The single-player
Play page reads `ext_data/dms.dat` for Jedi Outcast, gives the level's
name, passes the client game's import 34 to `startMusic` and
configstring 907 to it each frame, `s_allowDynamicMusic` too, and
starts the music again after a cinematic. Every single-player level
but `yavin_temple` (its file) and `valley` (its cutscene's music) has a
set, all their files there (`reluva-tool idtech3 music-sets`).

Differences: the original decodes ahead of what's heard (up to 16384
samples) and measures where a piece has got to from that *(inferred)*;
Reluva measures it from what's heard, so its exits come a little later.
The client game's "starting" call only loads in the original, the music
starting from the first snapshot; here it starts at once. The random
markers are Reluva's own draws. The `s_dynamic` console command and
`s_debugdynamic`'s prints aren't there.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the music start
  (0x427350), the reader (0x42bcb0, 0x42bd40, 0x42ca40) and its checks
  (0x42c35b on), the state read (0x428290), the change rules (0x42d6e0,
  0x4283a9, 0x42d730, 0x42d930), the playing (0x427c00, 0x427e50,
  0x427f10), the restarts (0x405122, 0x427310); `jk2gamex86.dll`: the
  music configstring's setters (0x200b0a56, 0x200b3f30), the client
  game's start (0x200333c0) and the state's update (0x20097c50). The
  GPL Jedi Outcast single-player source as a guide to the system's
  shape. `ext_data/dms.dat` and the pk3s' `music/` folders.
- **How it was checked:** a unit test (`idtech3.unit.sound`, "dynamic
  music") reads a made-up set (a `uses` hop, a missing transition
  refused) and plays it through the mixer: action at once, back to
  explore through action's transition at its exit time, silence through
  explore's, boss and death cutting in, nothing cutting death. A
  game-data step (`jk2-sp-music`, `music-sets`) reads every map's set
  from 1.02a's `dms.dat`, its files checked: 23 sets. Not yet heard in
  Reluva's window against the original.
