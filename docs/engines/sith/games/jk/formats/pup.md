# Jedi Knight: puppets (`.pup`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/puppet.cpp` · **Checked on:** US retail, all 49 puppets · **Confidence:** confirmed unless marked

## Overview

Which animation ([key.md](key.md)) a character plays for each thing it
does, in each of its modes: `ky.pup`, Kyle's, has a default mode, one
aiming a weapon, one with the saber, and three swimming. They are in
`Res2.gob`'s `misc\pup\`, plain text; a level's templates name each
character's.

## Layout

Line by line; a `#` starts a comment to the line's end.

- **`Mode=<n>`**, or **`Mode=<n>, BasedOn=<m>`**, in either case: a mode,
  and the mode whose actions it takes where it has none of its own.
- **An action:** `<name> <keyframe> <flags> <lopri> <hipri>`, as the
  header comment names them (`# submode keyframe flags lopri hipri`):
  what the character does (`stand`, `walk`, `fire`, `death`...), its
  `.key`, flags in hexadecimal (read with `%x`, so `0X28` too), and two
  priorities, low and high (`0x4e5180`) *(from the code)*. See
  [Behaviour](#behaviour).
- **`joints`**, then `<joint>=<node>` lines: which of the model's nodes
  are its head (0), neck (1), torso (2), and the weapons' firing and
  aiming points (3 to 6), as the comments name them.
- **`end`**: the last line read.

Every one of the 934 actions names an animation that's in the archives.

## Behaviour

An action is played on one of four tracks of the character's puppet
(`0x4e47d0`, `0x4e48b0`, `0x43e780`) *(from the code)*.

### Flags

The actions having each flag are counted in the files; what it does is
read from the code, and was watched on Kyle's puppet in the running game
(see [Sources and checks](#sources-and-checks) for how):

| Flag | What | Actions | Confidence |
|---|---|---|---|
| `0x1` | The animation doesn't run on time (its speed is set to 0, `0x4e4964`): the walking code moves it on as the character moves (`0x4e3e4e`), 280 frames for each unit (`0x4e3e70`). Seen: Kyle's walk, moved by a tenth of a second of the forward key, stopped at 280.5, 280.5 and 281.3 frames a unit moved in three runs; a Rodian's walk and run tracks had speed 0 too | 226: `walk`, `run`, `walkback`, the strafes, the crouched walks and turns | confirmed |
| `0x2` | It stops at its end, and its track is freed (`0x4e4935`, `0x43eb1e`). Seen: Kyle's `drawfists` freed at frame 7 (`kydrawft.key`'s last), and the level's first draw (1 and 3) at 10 (`kydrawgn.key`'s), each still at full weight: no fade | 8: the weapon draws (with `0x20`) and two `fire`s | confirmed |
| `0x20` | At its end it holds its last frame and fades out over a quarter of a second, then its track is freed (`0x4e493e`, `0x43eb47`, `0x43ee1c`). Seen: Kyle's `fire` held frame 7 (`kyfire.key`'s last) while its weight fell 4 a second, then its track was freed; `land` (`kyland.key`, 4 frames) and a `fidget` (`kyidle2.key`, 155) the same | 219: `hit`, `fire`, `land`, `fidget`... | confirmed |
| `0x4` | It holds its last frame (`0x4e494a`, `0x43eb34`). Seen: Kyle's `holster` held frame 5 (`kyhlstr.key`'s last) at full weight, its track kept, until the next weapon's draw took the track | 146: `rising`, `drop`, the deaths | confirmed; the deaths not seen |
| `0x8` | If the same animation is already on a track, it starts again there, not on another track (`0x4e48c5`, `0x43ee60`). Seen: Kyle firing again and again kept one `fire` track, which went back to frame 0 keeping its weight (1 at frame 0.8, where a new track weighs about a half) | 190: `hit`, `fire`, `land`, `victory`... | confirmed |
| `0x10` | It starts at full weight; otherwise it fades in over a tenth of a second (`0x4e496c`, `0x43e920`, `0x43e950`). Seen: 0.02 seconds into Kyle's `land` (`0x38`) it weighed 1, and the `stand` (`0`) begun with it 0.22; a `fire` weighed 0.54 at 0.05 seconds | 115: the deaths, `jump`, `land` | confirmed |

`0x2`, `0x20` and `0x4` choose what happens at the end, and only the
first of them set, in that order, counts (`0x4e4935`): the 14 of
`0x2C` fade out rather than hold, and the draws (`0x22`) were seen to
stop without fading. With none of them the animation loops
(`0x43eb66`), as the 357 actions of flags 0 do (`stand`, `fall`). No
action has another flag *(from the code)*.

When a character's movement action changes (`stand`, `walk`, `drop`...),
the one before fades out over a tenth of a second as the new one fades
in (`0x4e4ac3`): seen as a Rodian stopped (its walk at 0.71, its stand
at 0.29) and as Kyle landed (his `drop` at 0.78).

### Priorities

A track has the action's low priority on the model's nodes outside the
animation's `TYPE`, and its high priority on those in it; each node
follows the playing track with the highest priority: see
[key.md](key.md#priorities-and-body-parts) *(from the code)*. Of the
934 actions, 539 have 2 and 2, 92 have 0 and 0 (`stand`), 88 have 5
and 5 (the deaths); `fire` and `hit` mostly have 1 and 4 (64 of 90)
or 1 and 3, so they take the upper body from a walk (2) and leave it
the legs. Seen from Kyle's third-person camera: firing as he strafed
(`kyfire.key`, `TYPE 0xF`, 1 and 3, with `kystrafl.key`, 2 and 2, both
at full weight), his legs kept the strafe's stride and his arm the
shot's aim; strafing without firing, the arm hung by his side.

## In Reluva

`parsePuppet` (`src/engines/sith/formats/puppet.h`) reads one into a
`Puppet`; `Puppet::find` looks an action up in a mode and the modes it's
based on. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 49 files and their comments;
  `JK.EXE`'s reader (`0x4e5180`), the code that plays an action
  (`0x4e47d0`, `0x4e48b0`) and its tracks (`0x43e780` to `0x43ee60`).
- **How it was checked:** every puppet read by `reluva-tool sith
  formats`, every action's animation looked for in the archives;
  `sith.unit.models` on a puppet made by the test; the flags and
  priorities counted across all 934 actions with a script.
- **How it was watched:** some 45 runs of `reluva-tool sith host` into
  Nar Shaddaa, each stopped at a chosen moment after keys (`-keys`:
  forward, fire, jump, the fists) and with Kyle's puppet read by
  `-peek` as the run ended. The player's thing is the level's first
  (`0x8319a4`; the things at `0x8339c8`'s `+0xc4`, `0x2b0` bytes each),
  its puppet at its `+0xd8`; there four tracks of `0x134` bytes from
  `+8`, each with its flags (`+0`), priorities (`+8`, `+0xc`), speed in
  frames a second (`+0x10`), weight (`+0x18`), fade a second (`+0x1c`),
  frame (`+0x120`) and animation (`+0x128`, its name first)
  (`0x43e780` to `0x43ee60`) *(from the code)*. Kyle was in his gun's
  mode (1). Frames were saved with the third-person camera, bound to a
  letter in the player's file.

## Open questions

- A death (`0x14`) holding its last frame was not seen: no character
  was killed in the runs.
- Which track each of the model's nodes follows was seen only in the
  pictures; the per-node results weren't read from memory.
