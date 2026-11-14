# And Then There Were None: character shadows

> **Status:** done · **Code:** `src/engines/awe/render/shadows.cpp` (shared) · **Checked against:** the code (`ac.exe`) · **Confidence:** from the code unless marked

## Overview

The shadows are Orient Express's
([../../motoe/systems/shadows.md](../../motoe/systems/shadows.md)): each
character flattened onto the floor from a point 1500 units above its
scene's lights, marked in the stencil, the marked pixels darkened by a
black quad at alpha 63. Nothing in Reluva's shadows differs for this
game; what differs in `ac.exe` comes to the same with its data.

## How it works

Compared with `motoe.exe`'s with the objects' field offsets set aside:

- **The darkening** (`0x404290`, Orient Express's `0x402ba0`): the same
  code but for the frame's size, 800 by 600.
- **The shadow pass** (`0x421ec0`, Orient Express's `0x428250`): the same
  but for the light it casts from. With no "shadow only" lights (the
  light record hasn't the field: [lighting.md](lighting.md)) it's the
  mean of every light's position, raised by 1500, as Orient Express's
  without them. The rigid meshes are flattened the same way.
- **The pictures** clear the stencil where they're drawn, as Orient
  Express's (`0x42e6de`: a picture that doesn't scroll, its depth under
  10000, with shadows on).
- **The player's white-out** in the snow isn't drawn: the frame
  (`0x404510`) doesn't call it, and the cameras haven't its fields
  (their record stops at the 1240 field: [../formats/ac1.md](../formats/ac1.md)).
  Reluva's white-out needs those fields, so it's off here too.
- **The order:** the darkening is drawn last, after the dialogue box and
  the rest of the interface (`0x4045bf`), where Orient Express draws it
  before them. The interface's pictures clear the stencil as the scene's
  do, so the shadows don't show over them *(inferred: the dialogue
  window's picture is made by `0x42dc20`, whose draw isn't read)*.

## In Reluva

The shared code, unchanged: the light the shadows are cast from is the
mean of the lights other than shadow-only ones when there are any, and
this game has none.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s Direct3D calls
  (`SetRenderState` with the stencil states, `D3DRS_STENCILENABLE` 0x34),
  each function found so and compared with `motoe.exe`'s, instruction by
  instruction, field offsets aside.
- **How it's checked:** not seen running; no recording of this game has
  been compared yet.
