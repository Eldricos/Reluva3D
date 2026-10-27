# Character shadows

The characters' shadows in *Murder on the Orient Express* are planar.
Each character is flattened onto the floor away from one light, and the
pixels it covers are darkened by a constant amount.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the frame render (`0x402ed0`);
- the model mesh draw and its shadow pass (`0x428500` → `0x428250`);
- the darkening quad (`0x402ba0`);
- the pictures' stencil writes (`0x437b00`);
- the player white-out (`0x402e20`).

## What casts a shadow

Every mesh of a character that has a material (its textured meshes, not
helpers such as `Eye_L`) is drawn a second time just before its body.
The same goes for the meshes of models attached to the character. That
second draw writes no colour or depth; it only sets the stencil to 1
where it passes the depth test.

The world matrix for the second draw is `D3DXMatrixShadow`. It projects
from a **point light** onto the plane **y = 0**:

- the light is at the mean position of the scene's `LITE` records,
  raised by **1500** units;
- if any `LITE` has its last field (`shadowOnly`, +0xe1c) set, only
  those count (such lights light nothing: see [Lighting](lighting.md));
- with no lights, the light is at (0, 1500, 0);
- the lights' types, directions and colours aren't used, and neither are
  night, `OUTD` or the act.

A vertex v goes to

    s  = L.y / (L.y − v.y)
    v' = ( L.x + (v.x − L.x)·s,  0,  L.z + (v.z − L.z)·s )

The light is high, so the shadows lie close around the feet.

A character casts no shadow when its MODL `shadow` field is 0. The
`hideshadow` and `showshadow` verbs set that field. Footprints (`FOOT`)
don't touch the stencil.

## What it darkens

After the whole scene, one full-screen quad of colour ARGB `0x3F000000`
is alpha-blended where the stencil is 1. Shadowed pixels are multiplied
by **192/255**. Overlapping shadows don't darken further, and the edges
are hard.

The stencil is cleared in two places, so these pixels aren't darkened:

- **Bodies:** each character's body pixels reset the stencil (to 2 for
  the player, 0 for others), so a character isn't darkened by shadows
  drawn before it.
- **Pictures:** each 2D picture drawn later in depth order (SPRT `da4`
  0, depth below 10000) resets the stencil where its alpha isn't 0. A
  cutout in front therefore hides the shadows behind it.

Everything is switched by the **3D Shadows** option
([options.md](options.md)).

## The player's white-out in the snow

While shadows are on, the player's own pixels (stencil 2) are washed
toward white as they walk away from the camera. The amount comes from
the `CAMR` record's trailing fields `1244` (A) and `1248` (B), with z the
player's position:

- nothing while z ≥ A;
- alpha (A − z)/(A − B) between A and B;
- fully white past B.

Only the `Exterior.Train` and `Exterior.Forest` cameras set these.

## In Reluva

Reluva's renderer has no stencil buffer, so `shadows.cpp` builds the
mask on the CPU:

1. Each character's textured meshes, already skinned on the CPU, are
   flattened as above and projected through the scene's camera.
2. Their triangles are filled into a screen-sized mask, which gives each
   pixel once.
3. The mask goes to the screen as a black quad at alpha 63, after the
   background and before the characters, which cover their own pixels.
4. The white-out works the same way, with a mask of the player's own
   triangles.

**Differences from the game:**

- **Cutouts:** they sit in Reluva's 2D overlay above the 3D, so a shadow
  is never drawn over a cutout. In the game, the shadow of a character
  in front of a cutout darkens it.
- **Bodies:** a later character's shadow never falls on an earlier
  character's body.
- **Props:** the game's attachable cleaver and luggage meshes, which
  have one bone, cast their shadow near the world origin. Reluva flattens
  them where they are.
