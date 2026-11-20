# The Fallen: shadows, coronas, fog and the rest of a frame

> **Status:** partial · **Code:** `src/engines/unreal/render/level_view.cpp`, `render/mesh_render.cpp` · **Checked against:** the code (Render.dll) and recordings of the original · **Confidence:** from the code unless marked

## Overview

What the renderer draws besides the lit level ([lighting.md](lighting.md)):
the characters' shadows, volumetric lights, detail textures, mirrors,
coronas, translucent lit surfaces, the screen flash, distance fog, and
how it's all drawn.

## How it works

### Characters' shadows

The Fallen's characters cast shadows of their own shape: actors with
`bProjectsShadows` (pawns, weapons and pickups by default) from lights
with `bActorShadows` (every `Light` by default), when the renderer says it
draws them (`GetRendererFlags` bit 2; the game's blob shadows,
`CTV_Shadow`, then hide). How the original does it, read from its
renderer (Render.dll's `PrepareProjectedShadow`, `DrawProjectedShadow`
and its light manager's choice of light), and redone in Reluva's own code:

- **The floor.** A line straight down from the actor's `Location`, four
  times its `CollisionHeight` long; no shadow unless it hits something
  whose normal points up at least 0.7, on the side facing the camera.
  A held `Weapon` (`bHeldItem`, with an `Owner`) casts with its holder's
  light and place, faded by its own `ScaleGlow`. An actor whose
  first pass is blended (`bMultiTextureOn`, `MultiTextureOneStyle`
  `STY_Blended`; there's no `MultiTextureOne` texture, the first pass is
  its own skins) fades its shadow by
  `MultiTextureOneScaleGlow` instead (a cloak fading in).
- **Distance.** A factor of 900 / the floor point's depth in the view,
  at most 3.5; under 0.05 there's none.
- **The light.** Of the lights reaching the actor with `bActorShadows`
  (special lit ones only for a special lit actor), the one giving most at
  the floor point: `LightBrightness`/255 (times its `LightType`'s level
  now) x (1 - distance / radius) / (1 + the zone's
  `AmbientBrightness`/255). The light it had is kept unless the new one
  gives 0.02 more. The original's list of lights reaching an actor tests
  only their radius, not walls between.
- **Strength** (`ShadowBrightness`, the light in `ShadowLightActor`).
  Its target is that amount times the distance factor, at most 1. It
  moves towards the target by 1.3 x the seconds since the actor was last
  drawn (at most 1): up to it while the light stays the same; when the
  light changes, down to nothing, and the new light takes over once it's
  under 0.05. It jumps straight to the target if the actor wasn't drawn
  for 3 seconds, or either light isn't `bStatic`. (A player pawn standing
  still and not `Frozen` doesn't change it.) Under 0.05: no shadow.
- **Drawing.** The light is moved up along the floor's normal so it's at
  least max(2 x the larger of `CollisionHeight` and `CollisionRadius`,
  half its distance across) above the actor, which keeps shadows from
  low lights short. The mesh, as posed, is flattened from it onto the
  floor plane, raised 0.1 + 0.005 x depth off it, and multiplies what's
  there: each vertex by 1 - min(I / |its distance from the light - the
  actor's distance from the light|, 0.8), where I = min(strength x 50,
  35). The shadow is darkest (to 0.2) where it falls close to the actor
  and fades along its length.

Reluva leaves out the player-pawn rule. It draws overlaps once (the
renderer's StencilMode::Once) and has the per-vertex darkness in the
renderers (`gfx::ColorSource::kShadow`). Against a recording of the
original: Kira's shadow on M05_KiraL1's bridge floor falls the same way;
the original's reaches further, maybe from another light it kept. On the
Promenade shrine's platform, the light it chooses is one beyond a wall.

### Volumetric lights

A light with a `VolumeRadius` (as `LightRadius`: 25 x (it + 1) units) is
also a glowing sphere, in its hue and saturation at `VolumeBrightness`,
hiding what's behind it by `VolumeFog` (0-255). 37 of The Fallen's maps
have fog zones (`bFogZone`) or such lights, many both. The original
(`VolumetricLighting=True` by default) works them into a fog map for each
surface in a fog zone: for each texel, how much of each light's sphere
lies on the line from the camera to it, its density 1 - (d / r)^2 along
the line (Render.dll's surface setup, read for what it does), 1 for a
line straight through the middle; the light's colour times that added,
and what's behind dimmed by its fog times that (Direct3D blends fog maps
`ONE`, `INVSRCALPHA`), each at most the fog map's full 127.

Reluva does the same in a pass over the level's surfaces in fog zones
(a surface seen from a fog zone, flagged `PF_RenderFog` 0x40000000 on its
batch), working the line out per pixel rather than per texel; the
nearest 16 lights to the camera. Actors (meshes) and movers in a fog
zone (by where they stand) get the same pass over what they drew: the
original fogs meshes in fog zones too. The
lights' glow on Hass'terral's lab floor (M10_WorfL2, M10_SiskoL2: a row
of white volume lights under a grate) shows through the grate's gaps.
M07_SiskoL1A's lab, hazy in the recording, is in a zone without a
ZoneInfo, so no fog zone, and gets none: its haze is still unexplained.

### Detail textures

Not drawn: the original's Direct3D renderer has them off by default
(`DetailTextures=False` in `[D3DDrv.D3DRenderDevice]`; the OpenGL and
Glide renderers have them on), and the recording matches Direct3D's
display curve.

### Mirrors

A surface flagged Mirrored (poly flag 0x8000000) is a mirror: Sisko's
desk top in Ops (translucent and unlit), glass panels in M01_KiraL1A, 6
plain mirrors and 12 modulated glass panes in M01_SiskoL1B. As the
original's `OccludeBsp` does, each mirror seen from its front shows the
scene reflected in its plane (the original up to three reflections deep;
a translucent mirror only in high detail mode, `bHighDetailMode`, which is
usual). Reluva:

- marks where the mirror is in the stencil, then draws the level's
  surfaces seen through the plane (the view reflected in it, culling
  turned round), only there, with the projection's near plane moved onto
  the mirror's plane so nothing behind it shows;
- then draws the view as usual, a translucent or modulated mirror's own
  texture over the reflection, an opaque one only as depth (it shows
  just what it reflects);
- in the reflection the whole world: the level's surfaces, movers,
  actors and sprites (not characters' shadows, which use the stencil
  too, nor coronas), and the mirrors seen in it, up to three reflections
  deep as the original, each a stencil level up from the view it's in;
  at most four mirrors a view. After each the mark goes back down a
  level and the depth is cleared, so the view it's in draws what stands
  before the mirror over the reflection (Sisko before M01_SiskoL1B's
  mirror, his reflection in it).

### Coronas

As Render.dll draws them (the routine at `0x10b23cd4` and the one adding
each light, `0x10b24210`):

- **Which lights**: those permeating the BSP leaf the camera is in (the
  level model's leaf list) and the moving lights linked to that leaf,
  each with `bCorona` and a `Skin` (no Skin, no corona: not its
  `Texture`, a sprite's or an editor icon), not being deleted, and with a
  clear line from the light to the camera (the level's line check); at
  most 32 at once.
- **The glow**: each frame every corona's glow drops by the frame's time,
  and a seen one's rises by twice that, at most 1: in over a second, out
  over a second; at 0 it's gone.
- **Drawn** in front of the camera only, as its `Skin`, translucent (the
  device's tile with flags 0x104), over everything, centred where the
  light is on the screen, the frame's width x `DrawScale` x 0.8 pixels
  across; its colour the light's `LightHue` in the engine's three thirds,
  made whiter by `LightSaturation` / 255, times the glow (its
  `LightBrightness` not used). The credits station's flares
  (`coronaora02` at 0.025, `coronaora01` at 0.05) come to 19 and 38
  pixels on a 960-wide frame; their glow's visible part was about 15 and
  22 in a recording.

Reluva: a moving light (not `bStatic`) counts while the camera is within
its radius (in place of the leaf's links), and the line check is its
own trace through the level (`LevelView::addCoronas`).

### Translucent lit surfaces

A translucent surface the lights reach (not `Unlit`: The Fallen's title
logos, flagged translucent and special-lit, each lit by its own
`TriggerLight` that a dispatcher turns on and off) is drawn in one pass,
so it can't take its lightmap texel by texel: it's tinted by its light,
the average over its polygons' corners and middles of its lightmaps
(following its changing lights) and the dynamic lights, times two, at
most its texture's colour (fitted to the recording; the plain average
is 2.5 times too dark). So each logo fades in and out with its light,
the gaps between them as in the original, rather than all showing at
once, the Collective logo still there over The Fallen's.

### The screen flash

The player's `FlashScale` and `FlashFog`, which `PlayerPawn.ViewFlash`
works out each tick from the head's zone (`ViewFlash`, `ViewFog`) and
the flashes of being hurt or picking things up, tint the scene: every
pixel times the scale, plus the fog, under the HUD. Underwater (The
Fallen's `CTV_H20Zone`, ViewFog 0.13 0.20 0.17) that makes the view
blue-green throughout, as in the recording.

### Distance fog

The Fallen's ZoneInfo adds `DistanceFogging` and `DistanceFogColor` (the
FarClipping group, with `bForceClearScreen`). Eighteen maps set them,
mostly on their LevelInfo: Jerrado (3500 units, a near-black red-brown
17,4,0), Hass'Terral (5000, a dark blue), mission 11 (3500, 24,12,4).
As the original does it (Render.dll's `SetDistanceClipping` and
`SetDistanceFogging`, D3DDrv's `SetFogDistance`):

- The camera's zone's own settings: its ZoneInfo's, or for a zone
  without one the LevelInfo's. A ZoneInfo without them has no fog, not
  the LevelInfo's.
- Fog grows linearly with depth in the view, from none at the camera to
  all `DistanceFogColor` at `DistanceFogging` (Direct3D's linear table
  fog, start 0, end that distance). A pass over the level's opaque and
  masked surfaces and the meshes blends the colour over by depth
  (`drawFogOver`).
- Nothing beyond `DistanceFogging` is drawn: the far clipping plane is
  put there, unless the camera is in a sky zone. The sky, drawn from the
  sky zone, isn't fogged or clipped.

How far it reaches in the recording isn't measured: its views of those
maps didn't match ours closely enough.

### Drawing

The level's surfaces are one-sided, as UE1's are, unless flagged
`TwoSided` (0x100): a camera inside solid sees through the wall it's in.
M03_SiskoL1A's opening rides down inside the rock beside the lift shaft
and, in the original, looks in at the far wall's lamp and braces; drawn
two-sided, all it saw was the back of the near wall.

A mover's polygons (its brush's, drawn in their own coordinates) wind
the other way round to the level's, so they're culled the other way:
from inside M04_WorfL1a's turbolift (CTV_ElevatorMover2, whose brush has
144 untextured outer faces), culled as the level's, all that showed was
those faces from behind, white lit green.

Lightmaps are packed into 1024x1024 pages, each with a one-texel border
copying its edges so filtering doesn't bleed. They hold the light halved,
and are drawn over the surface's texture doubled (destination times
source times two); with the light capped at 1 that's the texture's own
colours at most, but the headroom is there for a game that brightens
past them. In the Maps page `L` turns them
off (shading by facing instead) and `[`/`]` dim or brighten them, for
comparing with the original.

## In Reluva

`render/level_view.cpp` (`LevelView`) draws a level, and
`render/mesh_render.cpp` its meshes.

## Sources and checks

- **Where the facts come from:** Render.dll, read for what it does,
  where named above, and the recordings of the original.
- **How it was checked:** against the recordings, as each section says
  (and [research/playthrough.md](../research/playthrough.md)).
