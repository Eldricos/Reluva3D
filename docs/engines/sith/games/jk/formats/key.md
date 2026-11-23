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
| `FLAGS <hex>` | 0, 1, 3 or `0xb` |
| `TYPE <hex>` | `0xFFFF` in most; `0xF`, `0x7F` and others in some: which of the body's parts it moves, as the models' node types *(inferred)* |
| `FRAMES <n>` | Its length in frames |
| `FPS <n>` | Frames a second: 15 in 498 files, 8 to 55 in the rest |
| `JOINTS <n>` | The model's node count; differs from the nodes below in 7 files |

### MARKERS

In 124 files: `MARKERS <n>`, then `<frame> <type>` lines: points the
game acts on (types 1 to 14; a footstep, a blow, by the names of the
animations that have them *(guessed)*).

### KEYFRAME NODES

`NODES <n>`, then each node: `NODE <i>`, `MESH NAME <name>` (the model
node it moves), `ENTRIES <n>`, and each entry on two lines:

| Line | Columns |
|---|---|
| First | `<i>:` frame, flags, x, y, z, pitch, yaw, roll |
| Second | dx, dy, dz, dpitch, dyaw, droll |

## Behaviour

At a frame, a node takes its last keyframe's position and rotation (in
place of its own in the model: Kyle's calf's first keyframe is his
model's calf position), each moved on by its change a frame for every
frame since. In all 504 files every keyframe's values are the one
before's moved on that way, angles taken round 360 degrees: 384,888
values checked. A node is matched to the model's by its name, in any
case (`K_Lcalf`, `k_lcalf`), and by its number where the names agree.

## In Reluva

`parseKeyframes` (`src/engines/sith/formats/keyframe.h`) reads one into
`Keyframes`; `Keyframes::at` gives a node's position and rotation at any
frame. The data viewer's Models page plays the animations whose nodes are
all the model's. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 504 files and their column comments;
  the models they move; the animations played in the data viewer.
- **How it was checked:** every animation read by `reluva-tool sith
  formats`; each keyframe predicted from the one before, as above;
  `sith.unit.models` on an animation made by the test.

## Open questions

- The header's flags and type, the entries' flags, and what each marker
  type means to the game.
