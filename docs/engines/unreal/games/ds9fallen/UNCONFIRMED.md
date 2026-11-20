# The Fallen: unconfirmed items

What Reluva does that hasn't been checked against the running original.
Nothing has yet been compared side by side with The Fallen running; a
full playthrough's recording has been
([research/playthrough.md](research/playthrough.md)).

## Read from the code, not seen running

- **Lightmaps, movers, the sky and the light effects' patterns** on The
  Fallen's maps: not compared with the original running
  ([lighting.md](systems/lighting.md), [actors.md](systems/actors.md)).
- **The effects:** swarms, lava, water rings, scripted paths. SR-III's
  firefly swarms are too small to see in the recording's opening scenes,
  and Jeraddo's bugs appear only in play ([effects.md](systems/effects.md)).
- **The transporter's look.** By the code (Render.dll and the shipped
  drivers) the beamed-in body's skin starts unseen and fades in as glow²,
  as Reluva draws it, and its column runs from the feet to 20 over the
  head. The recording shows the skin from the start and a lower, shorter
  column. Still to settle from closer footage
  ([research/playthrough.md](research/playthrough.md)).
- **Fire and water speed:** no fixed view of a fire or water texture
  found in the recording yet ([texture.md](formats/texture.md)).
- **Sound:** the reverb, Doppler and the provider's side, as read from
  Miles.dll ([sound.md](systems/sound.md)).

## Seen running, not traced in the code

- **Hass'terral's snowfield, evenly lit** in the recording, against the
  code's shadows: the fitted rule for large cylinder lights
  ([lighting.md](systems/lighting.md), [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)).
  Why the recording's field is evenly lit isn't known.
- **A gain left against the recording:** shown as the original's display
  shows it (the gamma ramp its Direct3D renderer sets from `Brightness`),
  Reluva is within 10-20% of the recording's cut scenes at every
  brightness; what's left is about the same everywhere, a gain rather
  than a curve, not found yet ([lighting.md](systems/lighting.md)).
- **Overall brightness:** the recording runs brighter than Reluva by 1.2
  to 2.3 times in mean level, varying by scene (a brightness setting, its
  renderer, or both) ([research/playthrough.md](research/playthrough.md)).
- **What lights the credits station** (CM_SpaceStation): by Render.dll an
  empty list of lights leaves a surface to its zone's ambient (black
  here) and dynamic lights, and there are none near it; yet the
  recording shows its rings' outward faces lit. Its lights look moved
  since its last lighting build ([lighting.md](systems/lighting.md)).

## Inferred

None yet.

## Guessed

None yet.

## Settled

- **The animated textures' settings** (November 2026): their pacing,
  priming, flipbooks and surfaces' panning, from Engine.dll and
  Render.dll ([texture.md](formats/texture.md)).
- **The effects' physics modes** (November 2026): each read from its
  function in Engine.dll ([effects.md](systems/effects.md)).
- **Meshes drawn from packed vertices** (November 2026): Engine.dll's one
  fixed swap of axes is Reluva's turn ([mesh.md](formats/mesh.md)).
- **Which pawns move in which modes** (November 2026): each pawn class's
  physics switch in Engine.dll ([movement.md](systems/movement.md)).
- **Noises the AI hears** (November 2026): Engine.dll's `PlaySound` and
  `PlayDummySound` rules; `MakeNoise` makes none ([ai.md](systems/ai.md)).
- **UnrealEd's cameras** (November 2026): flagged editor-only in the
  maps, so the game never creates them ([level.md](systems/level.md)).
- **The bones' axes and the IK** (November 2026): the axis swap,
  `C3DApplyRotation`'s and `C3DApplyWorldRotation`'s order, and
  `C3DApplyIK`'s chains, joint limits and solve are as Engine.dll has
  them ([bones.md](systems/bones.md)).
- **Character normals** (November 2026): Render.dll makes each vertex's
  normal from the triangles' winding alone; Reluva had guessed which way
  was out from each mesh's middle, which turned the EVA suit's inward.
  Now as Render.dll, and lit as in the recording
  ([research/playthrough.md](research/playthrough.md)).
- **Type 2 attachment points** (November 2026): Engine.dll settles that
  the stored matrix is in the skeleton's posed space
  ([mesh.md](formats/mesh.md)).
- **Each zone's own sky** (November 2026): as `ZoneInfo.LinkToSkybox`
  links it, as Arduria's opening in the recording shows
  ([research/playthrough.md](research/playthrough.md)).
- **Cylinder lights on actors** (November 2026): shaded from the actor's
  own height, after Sisko was drawn black in the SR-III capture
  ([research/playthrough.md](research/playthrough.md)).
- **The fits to the first recording** for light colours, characters'
  light, the falloff and the display curve were replaced by what the
  original's code does ([lighting.md](systems/lighting.md)).
