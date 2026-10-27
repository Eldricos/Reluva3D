# Character shadows

The characters' shadows in *Murder on the Orient Express* are planar.
Each character is flattened onto the floor away from one light, and the
pixels it covers are darkened by a constant amount.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the frame render (`0x402ed0`) and the scene's draw in depth order
  (`0x435d90`, the depth `0x4104b0`);
- the model mesh draw and its shadow pass (`0x428500` → `0x428250`), and
  where a mesh is made rigid (`0x427ff5`, `GrannyMeshIsRigid`);
- the darkening quad (`0x402ba0`);
- the pictures' stencil writes (`0x437b00`, in the picture draw
  `0x437ac0`; video loops draw through it too, `0x4062cb`) and the
  particles' draw, which has none (`0x42d470`);
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

### Rigid meshes

Granny keeps a mesh whose vertices have no bones of their own as a
*rigid* mesh: the whole mesh follows one bone. The game draws one with
that bone's matrix C as the world matrix (`0x4285f0`), and its shadow
pass then:

- takes C's translation P as the point the plane goes through, so the
  plane is y = P.y, not the floor;
- adds P to the lights' sum (but not to their count): the light is
  (P + Σ positions) / n, raised by 1500;
- replaces the world matrix with the shadow matrix, so C is dropped.

The shadow is therefore the mesh's own shape (as it was modelled, about
the model's origin), flattened at the bone's height, near the world's
origin, wherever the prop is.

Every character body is skinned. The only rigid meshes with a material
in `Models/` are the attachable cleaver and luggage (`Objects/cleaverbad`,
`cleavergood`, `luggage1` to `luggage3`; the same cleaver in `Klaus/`),
the footprints, the goats' shadow planes and an unused Debenham model.
No shipped script attaches the cleaver or the luggage (Klaus's cleavers
are a skinned model of their own), so the stray shadow only shows if one
is attached with `attach luggage1` and the like.

## What it darkens

After the whole scene, one full-screen quad of colour ARGB `0x3F000000`
is alpha-blended where the stencil is 1. Shadowed pixels are multiplied
by **192/255**. Overlapping shadows don't darken further, and the edges
are hard.

The stencil is cleared in two places, so these pixels aren't darkened:

- **Bodies:** each character's body pixels reset the stencil (to 2 for
  the player, 0 for others), so a character isn't darkened by shadows
  drawn before it.
- **Pictures:** each 2D picture drawn later in depth order (SPRT `scroll`
  0, depth below 10000) resets the stencil where its alpha isn't 0,
  even where it is nearly transparent. A cutout in front therefore hides
  the shadows behind it. Video loops do the same; fog and cloud layers
  (`scroll` set) and particles don't.

The scene's objects are drawn back to front by their depth: a picture's
`depth` if set, else its z; a character's z, where it stands. Where two are
equal, the scene's order holds. So which cutouts a shadow darkens comes
down to the depths:

- A cutout less deep than the character (drawn before it) is darkened
  by its shadow, as the floor is. The character is drawn over the
  cutout, too.
- A cutout deeper than the character (drawn after it) takes the shadow
  off under its pixels, and is never darkened by it.

A shadow is depth-tested against the models drawn before it, so it can
also fall on an earlier character's body where the flattened shadow is
nearer the camera than the body. It is not tested against pictures,
which write no depth.

Everything is switched by the **3D Shadows** option
([options.md](options.md)).

## The player's white-out in the snow

While shadows are on, the player's own pixels (stencil 2) are washed
toward white as they walk away from the camera. The amount comes from
the `CAMR` record's trailing fields `whiteoutStart` (A, at +0x1244) and
`whiteoutEnd` (B, +0x1248), with z the player's position:

- nothing while z ≥ A;
- alpha (A − z)/(A − B) between A and B;
- fully white past B.

Only the `Exterior.Train` and `Exterior.Forest` cameras set these.

## In Reluva

Reluva draws the frame in the game's order:

1. The pictures are put in layers between the characters, by the depths
   above (`ScenePage::draw`). The first layer, behind every character,
   holds the background; each other layer comes after one character.
   Each layer is a texture over the whole screen.
2. The characters are drawn in depth order, each followed by its layer.

Reluva's renderer has no stencil buffer, so `shadows.cpp` keeps the
game's stencil on the CPU (`ShadowStencil`): a value and a depth for
each pixel. As each character is drawn, its meshes, already skinned on
the CPU, are projected through the scene's camera and filled in:

- each textured mesh's shadow, flattened as above (rigid ones as the
  game does), sets the stencil to 1 where it passes the depth test, and
  writes no depth;
- then its body sets the stencil to 2 for the player and 0 for others,
  and writes its depth;
- the props attached to it follow, the same way;
- then the layer drawn after it sets the stencil to 0 wherever one of
  its pictures that resets the stencil has alpha.

When every character is drawn, the pixels whose stencil is 1 go to the
screen as a black quad at alpha 63, over the characters and the
pictures. The pixels whose stencil is 2 make the player's white-out.

The depth test uses the CPU's own depths of the characters' triangles,
not the renderer's. Pictures write none, as in the game.

**Differences from the game:**

- The CPU fills triangles at pixel centres, which may differ from the
  graphics card's edges by a pixel.
- A triangle with a corner behind the camera is left out of the stencil.
