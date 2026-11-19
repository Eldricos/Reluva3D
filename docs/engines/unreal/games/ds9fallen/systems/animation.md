# The Fallen: animation

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_anim.cpp` · **Checked against:** the code (Engine.dll) · **Confidence:** from the code unless marked

## Overview

Actors' animations play on the scripts' timing, with their
notifications: layers on parts of the body, faces (blink, brows, mouth),
tweens from the pose showing. Where the meshes' animations come from is
in [mesh.md](../formats/mesh.md); bones and attachment points in
[bones.md](bones.md).

## How it works

`vm_anim.cpp` plays actors' animations as the scripts ask, each tick
after physics. How the natives and the tick behave was checked against
Engine.dll (`execPlayAnim`, `execLoopAnim`, `execTweenAnim`,
`execFinishAnim`, `AActor::UpdateAnimation`, and The Fallen's `C3D`
layer natives); the code is Reluva's own.
An actor's animation is its own properties:
- `AnimSequence`.
- `AnimFrame`: 0 to 1 through it; below 0 while tweening into it.
- `AnimRate`: how much of it a second.
- `bAnimLoop`, `bAnimFinished`, `AnimLast` (where its last frame
  starts), `TweenRate`.

The sequences are the actor's mesh's: its own, and those it names
whose frames are in the shared sets (`Mesh::otherAnims`), each with a
frame count and frames a second.

- `PlayAnim(Sequence, Rate, TweenTime)` plays it once: `AnimRate` is
  `Rate` times the sequence's frames a second over its frame count.
  `TweenTime` left out is -1: tween only if another sequence was showing
  (over one frame's time), else start at once. A tween sets `TweenRate`
  to 1 / (time × frames) and `AnimFrame` to minus one frame; with none,
  `AnimFrame` starts at 0.001 (0.0001 looping) so the first frame's
  notification isn't missed. A one-frame sequence just tweens. Each
  call keeps `OldAnimRate` and the frame it tweens from
  (`TweenAnimFrame`). While `AnimFrame` is below 0 the mesh is drawn
  blended from that frame to the first. A sequence the mesh doesn't have
  changes nothing.
- A negative rate is scaled by the actor's speed (as the public
  UnrealScript documentation has it): each unit a second of `Velocity`
  plays `-Rate` times the sequence's speed, never slower than
  `LoopAnim`'s `MinRate` times it (`AnimMinRate`). No script of The
  Fallen's passes one.
- `LoopAnim` does the same, looping. If it's looping that sequence
  already, only its rate changes (scripts call it every tick).
- `TweenAnim(Sequence, Time)` goes to its first frame over `Time` and
  stays; with no time it's there at once, not animating.
- `IsAnimating`: a sequence, and `AnimRate` (playing) or `TweenRate`
  (tweening in) not 0.
- Each tick moves `AnimFrame` on, in up to four steps when a step ends
  something (a tween landing, a notification, the end).
  - Passing a notification's time calls the function it names on the
    actor. These are the mesh's own, kept with each sequence (78 of
    The Fallen's meshes have them, 3,190 in all): footsteps, AI firing
    their weapons (`FireWeapon`), the jump's push (`ApplyJumpVelocity`),
    and more. No script sets `bAnimNotify`, so they're called whenever
    a sequence has them, each one passed since the last tick (after
    the old frame, up to and with the new), in order.
  - At the end (1.0), `AnimEnd` is sent. A loop goes on round and sends
    `AnimEnd` when it wraps (not when it began past the end). One
    played once stops at its last frame, finished.
- `FinishAnim` (latent: The Fallen's scripts call it 638 times) clears
  `bAnimLoop` and `bAnimFinished`, then waits for the end: a loop plays
  out its round and stops. It returns at once if nothing's playing. `IsAnimating` says whether one
  is playing or tweening. `GetAnimDuration` gives a sequence's length
in seconds at a rate. `HasAnim` and `GetAnimGroup` read the
  sequences, borrowed ones included (`runf` is Kira's, not only her
  mesh's own).

On M01_KiraL2:
- Kira's idle loops.
- Running plays `runf`, round every two-thirds of a second.
- Stopping plays an idle look-around.
- Jumping plays `Jump` (0.4 s), whose notification pushes her up about
  77 units. Its `AnimEnd` then starts the in-air loop, `LeapIdleU`.

### The Fallen's layered animation

The Fallen's meshes built on skeletons can play an animation on part of
the skeleton over the actor's own. Its scripts do it through `C3D`
natives:
- **Melee:** a punch or weapon swing while running (`CTV_Human`, on
  `UpperTorso`, from `Bip01_Spine`).
- **Using things:** the player's activate gesture.
- **Cut scenes:** the animators' upper or lower body.

`vm_anim.cpp` keeps each actor's layers, one per group.
- **The groups are the mesh's** (Engine.dll's): each bone lists the
  animation groups it's in (the first array of its record), the mesh
  names them (`FullBody` 0, `LowerTorso` 1, `UpperTorso` 2). On Male,
  the pelvis and legs are in LowerTorso, the spine and up in
  UpperTorso, every bone in FullBody. A layer poses its group's bones;
  of the layers on a bone, the highest group's wins it outright (the
  actor's own animation, FullBody, under them all). A mesh with no
  group list gets Reluva's own split (the InheritBone and those under
  it; a group named lower, the rest).
- **Starting:** `C3DPlayAnim(Group, Sequence, Boned, Rate, TweenTime,
  InheritBone, ...)` and `C3DLoopAnim` play the sequence on the group's
  bones. The InheritBone and its clears are set only when a bone is
  given; else they stay as they were. It fades in over the tween (0.2 s
  when left out), its weight rising from 0 to 1, its time held at the
  start until it's in. The rate is a plain multiplier of the
  sequence's own speed.
- **Holding a pose:** `C3DTweenAnim` fades to the first frame.
- **Fading, as Engine.dll does:** a layer fading in or out blends its
  bones from the pose showing when the fade began (each bone in its
  parent, before IK), held there: in, toward the layer's pose; out
  (`C3DTweenOutAnim`), toward what's under it now, as its weight falls.
  So a layer started over one that shows (a combo's right punch into the
  left) fades from where the arm was, the old one with it. A saved game
  loaded mid-fade has no such pose: that fade crossfades by weight from
  what's under, the layer it replaced held there.
- **Keeping the actor's turn:** `ClearYaw`, `ClearPitch` and `ClearRoll`
  act on the layer's InheritBone alone, as Engine.dll does: its turn
  from its bind pose in the skeleton's space, split into a turn about
  one of the skeleton's axes (Y yaw, X pitch, Z roll) and the rest. One
  cleared: that turn taken off; two: only the third axis's kept; all
  three: the bone as in its bind pose (where it is kept). Blended in by
  the layer's weight as it fades. The bones below take the layer as it
  is. `CTV_Human`'s melee sets all three except when swimming.
  (`C3DRemoveRotation` is another thing: Euler angles.)
- **Speed:** `C3DLoopAnim`'s `MinRate` is ignored, as the engine does.
- **Ticking:** a layer moves on at its rate and calls the sequence's
  notifications. One played once sends `C3DAnimEnd(Group)` at its end
  and holds its last frame; a loop sends none as it goes round, nor
  does a tween. The scripts answer `C3DAnimEnd` with
  `C3DTweenOutAnim(Group, 0.1)`, which fades it out, and then it goes.
- **Waiting and asking:** `C3DFinishAnim(Group)` (latent) stops a loop
  looping and waits for that end, and `C3DIsAnimating(Group)` says
  whether one's playing.

Faces are channels, as Engine.dll keeps them (after the mesh's own
animation groups, in the actor's C3D mesh state):
- `C3DPlaySkinAnim()` adds one and names it `sk` and its place among the
  actor's groups (the mesh's FullBody, LowerTorso and UpperTorso first:
  Pawn's `Phonemes`, `Blink`, `EyeBrows` and `Mouth` channels are `sk3`
  to `sk6`), counting up while another channel has the name.
- `C3DSetSkinAnim(Tag, Sequence, FrameTime, Weight)` sets a channel to a
  time in a face sequence (seconds: the scripts pass frame × 0.033) at a
  weight, at once.
- `C3DTweenSkinAnim(..., TweenTime)` cross-fades from what the channel was
  set to last (not what shows mid-tween; the old time read in the new
  sequence) to the new, straight over `TweenTime`: the two shapes fade,
  the time doesn't sweep through those between. `C3DTweenSkinAnimDelay`
  is the same from the channel's next tick (the last asked wins): the
  lip-sync's `C3DSpeechEvent` uses it, as it comes from the audio driver.
- A channel's time never moves on its own. One held at time 0 shows
  nothing.
- `C3DClearSkinAnim` removes one. `C3DCopySkinAnimState(Other)` gives each
  of this actor's channels the state of the other's of the same tag;
  none made or removed.
- The face sequences: Blink 2 keys (0, 1/30 s), EyeBrows 3, Phonemes 8 (k/30
  s, a mouth shape each), Mouth (four PAWNAI meshes; no script uses it).
- **Drawn** as the engine does it, after the body is posed: each channel's
  entry in the mesh's face table (the clip of its name, by index; see
  [mesh.md](../formats/mesh.md)) at its time, between keys
  blended, placed in its attachment point's frame (the `FaceAttach`
  triangle on the posed mesh, so it follows the head), and each vertex it
  lists pulled there by the channel's weight from where the body put it.
  At the scripts' weight of 1 those vertices are the keyed shape. A tween
  cross-fades that from the channel's last time and weight. Channels go
  in turn, each from the last's result.
- The engine sends `UpdateHeadAnimation` to each pawn with one of
  The Fallen's meshes every tick. It sets the face's expression and
  blinks, and where the head looks. It returns at once for a pawn not
  drawn in the last four seconds (`LastRenderTime`, which the Maps
  page's drawing sets).
  - To pick whom to look at, it goes through the pawns within 800 units
    (`RadiusPawns`). It weights each by how near it is, more for the
    one talking and the enemy, times what `CheckFOV` says of it.
  - It then turns the head (`C3DApplyWorldRotation` on `Bip01_Head`)
    toward the chosen pawn's head, eased and kept within limits.
  - On DS9_Ops, up to 12 heads look at someone at once. Each head is on
    average 22° off the line to its target, where the body's facing is
    37° off.
- `CheckFOV(Pos, Rot, Target, Radius, CosFOV, out BestHitLocation,
  bCheckExtents, bCalcVisible, LocationZAdjust, HeightAdjust)` says how
  much of `Target` is seen from `Pos` looking along `Rot`. The AI's sight
  uses it, and so do the turrets, the player's auto-aim and the heads.
  It works as Engine.dll's does. The scripts compare the result against
  0, 0.1, 0.3, 0.5 and 0.99, and the heads multiply a weight by it.
  - **The target's middle** is its Location raised by `LocationZAdjust`
    (the heads pass the head's height). H is its `CollisionHeight` +
    `HeightAdjust`.
  - **It returns 0** unless the middle is closer than `Radius` and the
    cosine of its angle off `Rot`'s forward axis is over `CosFOV`.
  - **A point is seen** if a line to it (through the world, movers and
    actors, but not the looker) meets `Target` first. So another pawn in
    the way hides it too.
  - **Without `bCalcVisible`** (or with H 10 or less and no
    `bCheckExtents`), the middle is tried first: seen gives 1. If it
    isn't, and H > 10 or `bCheckExtents`, the grid below is tried, and 1
    comes at the first point seen.
  - **The grid:**
    - two rows at ±0.9 H if H > 10, else one at the middle's height;
    - with `bCheckExtents`, two points a row, 0.9 × `CollisionRadius`
      either side, across `Rot`'s forward axis (level, and shorter the
      steeper the look);
    - the lower row is a step further over, as the original's loop leaves
      it.

    With `bCalcVisible` it returns the share of the grid's points seen
    (quarters or halves; the middle isn't one of them).
  - **`BestHitLocation`** is the first point seen. The turrets aim there.
    If none is seen it's left as it was.
- `C3DUsesC3DMesh` says whether the mesh is in The Fallen's own layout
  (its imported-model block), and `C3DIsBoned` whether it's built on
  bones. `Pawn` sets its face's channels up only if the first is true.

Drawing a layer (`LayeredPoser`, `mesh.cpp`): the frames posed when a
mesh is read are one animation each, so an actor with a layer showing
is posed at draw time instead.
- Each bone takes its place in its parent from the actor's own
  animation. Those of the layer's part are blended towards the layer's
  animation's by its weight.
- Keys come from the mesh's own clips, or the shared set's for an
  animation past its own frames, as borrowing does.
- The vertices are placed as reading does, and that frame is uploaded
  for the frame drawn.
- With no layer, it gives exactly the frames read: no difference on
  Kira's idle, run, or the punch borrowed from the shared sets.

On M01_KiraL2, firing while running throws `MelRunPnchL`. The frame
drawn mid-punch has her legs exactly as in the run without it, and her
upper body turned into the punch.

Drawing a face: its channels are the vertex-key clips (`Blink`,
`EyeBrows`, `Phonemes`, [mesh.md](../formats/mesh.md)). Each
clip's first key is the same neutral face, in `BaseReference`'s pose (on
Quark to within a few units, at the hands); the later keys move 8 to 32
of his 377 vertices. A channel is its clip's key at its time (between
two, blended) less the neutral face, times its weight. Each vertex's
share is turned by its bones from `BaseReference`'s pose to the one
drawn and added to the posed vertices. A channel at the clip's start
adds nothing, so only the faces away from neutral are posed at draw
time. On DS9_Ops the scripts set Phonemes 0.07 to 0.1 s in, EyeBrows
0.07 and Blink 0.01: Dax's lids drop, her brows lift and her mouth
changes shape.

Not done:
- **Lip sync:** the game has no lip-sync data, so Phonemes is only ever
  where the head's expression script puts it.

## In Reluva

`vm/vm_anim.cpp` plays the animations each tick after physics.

## Sources and checks

- **Where the facts come from:** Engine.dll's `execPlayAnim`,
  `execLoopAnim`, `execTweenAnim`, `execFinishAnim`,
  `AActor::UpdateAnimation` and The Fallen's `C3D` layer natives, read
  for what they do.
- **How it was checked:** in play, and cut scenes against a recording
  ([research/playthrough.md](../research/playthrough.md)).
