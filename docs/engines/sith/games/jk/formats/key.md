# Jedi Knight: animations (`.key`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/keyframe.cpp` · **Checked on:** US retail, all 504 animations · **Confidence:** confirmed unless marked

## Overview

How a model's nodes move: walking, firing, a door opening, a ship
flying past. They are in `Res2.gob`'s `3do\key\`, plain text, written by
the same tool as the models ([3do.md](3do.md)); the puppets
([pup.md](pup.md)) say which a character plays for what.

## Layout

Line by line; a `#` starts a comment to the line's end.

### HEADER

| Line | What |
|---|---|
| `FLAGS <hex>` | Written in hexadecimal: `0x0000` in 338 files, `0x0003` in 163, `0x0001` in 2, `0x000b` in 1. The game reads it as a decimal number (`flags %d`, `0x44ad44`), so it stops at the `x` and every retail file's flags read as 0. The value is kept at byte `0x24` of the animation, and nothing reads it there but the format's writer (`FLAGS 0x%04x`, `0x44b258`), which nothing in `JK.EXE` calls *(from the code)*. What it meant to the tool that wrote the files can't be told from the game, and changes nothing in it |
| `TYPE <hex>` | Body parts, as the bits of the models' node types ([3do.md](3do.md#hierarchydef)): `0xFFFF` (all) in 393 files, `0xF` (torso, arms, head) in 91, `0x7F` (the seven parts) in 16; `0x4` (right arm), `0x1D`, `0x1F`, `0x7D` in one each. They choose which priority each node takes: see [Priorities and body parts](#priorities-and-body-parts) |
| `FRAMES <n>` | Its length in frames: the track loops, stops or holds when it gets there (`0x43eb1c`) *(from the code)* |
| `FPS <n>` | Frames a second: 15 in 498 files, 8 to 55 in the rest |
| `JOINTS <n>` | The model's node count; differs from the nodes below in 7 files |

### MARKERS

In 124 files: `MARKERS <n>`, 1 to 5 of them, then `<frame> <type>`
lines: points the game acts on, types 1 to 14 but 7. The game reads the
frame as a decimal fraction and takes 8 markers at most: a file with
more doesn't load (`0x44aeee`) *(from the code)*. What each type does:
see [Markers](#markers).

### KEYFRAME NODES

`NODES <n>`, then each node: `NODE <i>`, `MESH NAME <name>` (the model
node it moves), `ENTRIES <n>`, and each entry on two lines:

| Line | Columns |
|---|---|
| First | `<i>:` frame, flags, x, y, z, pitch, yaw, roll |
| Second | dx, dy, dz, dpitch, dyaw, droll |

An entry's flags, in hexadecimal, say what changes until the next
entry *(from the code)*: `0x1` the position, moved on by dx, dy, dz a
frame (`0x43f0c8`), `0x2` the rotation, by dpitch, dyaw, droll
(`0x43f119`); without the flag the value is held. Of the 72,587 retail
entries, 47,648 have 2, 12,684 have 0, 10,073 have 3 and 2,182 have 1,
and every change the flags leave out is 0. The game reads the frame as
a decimal fraction (`0x44b0dd`); every retail one is whole.

## Behaviour

At a frame, a node takes its last keyframe's position and rotation (in
place of its own in the model: Kyle's calf's first keyframe is his
model's calf position), each moved on by its change a frame for every
frame since. In all 504 files every keyframe's values are the one
before's moved on that way, angles taken round 360 degrees: 384,888
values checked. A node is matched to the model's by its name, in any
case (`K_Lcalf`, `k_lcalf`), and by its number where the names agree.

### Priorities and body parts

A character plays up to four animations at once, each on a track of its
puppet with the two priorities its action gives ([pup.md](pup.md))
*(from the code)*. For each of the model's nodes, a track takes its high
priority where the node's type is one of the animation's `TYPE` bits,
and its low one elsewhere (`0x43f049`). The node then follows the
playing track with the highest priority; tracks of the same priority
are mixed by their weights, and a lower one shows only while those
above weigh less than 1, fading in or out (`0x43f07d` to `0x43f37e`).
Kyle's `fire` (`kyfire.key`, `TYPE 0xF`, priorities 1 and 3) so takes
his torso, arms and head from his walk (2), and leaves it his legs, as
`ky.pup`'s comment has it: "firing (upper body only)".

Read from memory: a character's node matrices are kept at offset `0x20`
of its model instance (offset `0xc4` of its thing; the puppet at its
`0x14`), one 3 by 4 matrix a node, in the world, the mesh moved by its
pivot (written at `0x43f610`, the instance passed to `0x43eeb0`). Kyle
killed, his death at priority 5 and another track at 0 both weighing 1,
each node's matrix against its parent's was `kygndth0.key`'s last
frame, its values in place of the model's: within 0.002 in rotation and
0.00004 in position, for all 17 nodes under the root.

### Markers

As a track's frame passes a marker, the puppet's handler (`0x4e4b10`)
acts on its type; which files have each is counted in them:

| Type | In | What the game does | Confidence |
|---|---|---|---|
| 1, 2 | 14 walks | A footstep walking, left (1) or right (2): the thing's sound class's `lwalk` or `rwalk` sound for what it stands on (hard, metal, water, puddle, earth) (`0x4e4b53`) | from the code |
| 8, 9 | `kyrun0` to `kyrun2`, `gmstick` | The same running: `lrun`, `rrun` | from the code |
| 6, 13 | `kysbback`, `kysbswim` | A swimming stroke, left (6) or right (13): `lswimsurface` or `rswimsurface` at the surface, else `lswimunder` or `rswimunder` (`0x4e4cac`, `0x4e4d0e`) | from the code |
| 7 | None | Treading water: `treadsurface` or `treadunder` (`0x4e4d70`) | from the code |
| 10 | 38 deaths | The thing's `corpsehit` sound (`0x4e4dd2`) | from the code |
| 3 | 26 shots, blows and bites | A character the AI runs fires its weapon: the code reads the AI's state and the thing's weapon templates (`0x4e4e57`, `0x4eba10`) | inferred |
| 4, 5 | 30 saber swings | The blade starts (4) and stops (5) hitting what it passes through (`0x4e4c82`, `0x4e4c90`, read at `0x40b95e`) | from the code |
| 14 | 8 of those swings | The blade forgets what it has hit, so the same swing can hit it again: it empties the two lists the blade fills as it hits (`0x4e4c9e`, `0x40b944`) | inferred |
| 11, 12 | 11 jumps; 5 leaps and flips | A jump, at the thing's jump speed (11) or twice it (12) (`0x4e4e17`, `0x4fc270`) | inferred |
| 0 | None | Frees the track number the puppet keeps for idle fidgets (`0x4e4b28`) | inferred |

Footsteps and strokes sound only from a track weighing at least a half
(`0x4e4b6e`), and only for a thing with a sound class
([snd.md](snd.md)). The other types are ignored (`0x4e4b1b`).

## In Reluva

`parseKeyframes` (`src/engines/sith/formats/keyframe.h`) reads one into
`Keyframes`; `Keyframes::at` gives a node's position and rotation at any
frame. The data viewer's Models page plays the animations whose nodes are
all the model's. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 504 files and their column comments;
  the models they move; the animations played in the data viewer;
  `JK.EXE`'s loader (`0x44aca0`), its tracks (`0x43e780` to `0x43ee60`),
  their blending (`0x43eeb0`) and the puppet's marker handler
  (`0x4e4b10`), with the sound names in its table at `0x54b408`; its
  writer of the format (`0x44b1f0`), which nothing calls.
- **How it was checked:** every animation read by `reluva-tool sith
  formats`; each keyframe predicted from the one before, as above;
  `sith.unit.models` on an animation made by the test; the header's
  values, the entries' flags against their changes, and the markers
  counted across all 504 files with a script. Readers of the kept flags
  were searched for at every place that holds an animation: the
  tracks (`0x43e780` to `0x43f400`), the level's list of them
  (`0x4e5810`, `0x4e3080`), the cog verbs `PlayKey`, `GetKeyLen` and
  `jkPlayPovKey`, the code that starts a track from data read back or
  sent (`0x40d010`, `0x4f3ad6`, `0x4f9e88`) and the animations the
  game loads for itself (`0x419143`); none reads byte `0x24`.

## Open questions

None.
