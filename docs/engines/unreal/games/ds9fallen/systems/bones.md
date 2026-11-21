# The Fallen: bones and attachment points

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_attach.cpp` · **Checked against:** the code (Engine.dll) and the meshes' precomputed places · **Confidence:** from the code unless marked

## Overview

Where parts of an actor's mesh are, for what it holds, the scripts'
bends and IK.

## How it works

`vm_attach.cpp` says where parts of an actor's mesh are: its bones, its
attachment points and its precomputed places ([mesh.md](../formats/mesh.md)).
Each is worked out in the mesh's animation now:
- **The pose:** posed through its skeleton with its layers (once a tick,
  kept for the tick), or between its frames. A bone moves with whatever
  layer plays on its part: mid-punch, Kira's left hand is 30 units
  further forward than standing, while her feet don't move.
- **Placing it:** placed as the Maps page draws the mesh, so they agree
  (`placeMeshVertex`, then the actor's Location, PrePivot, Rotation and
  its scale), through the inverse of Engine.dll's `C3DGetWldToC3DTM`
  (0x1032d600; 0x1032d3d0 for joints and attachment points). That
  matrix takes off Location + PrePivot (both, the same sign as the
  frame builder: no actor is moved twice by its PrePivot), undoes
  Rotation then PreRotate, and scales each of the actor's axes by
  AxisScale / DrawScale: DrawScale 1 for a `bParticles` actor, each at
  least 0.001. That undoes how the mesh is drawn (DrawScale times
  AxisScale) only where AxisScale is 1; Reluva keeps the original's
  quirk, which only a squashed Arachnid corpse shows. The IK targets
  are taken into the skeleton's space by the same matrix.

The natives:
- `C3DGetJointWorldPos(Bone, out Location, out Rotation)`: a bone's place
  and axes. The cut-scene cameras aim at a speaker's `Bip01_Head`.
  `C3DGetJointLocalPos` gives it in the skeleton's own space, with only
  its axes swapped to Unreal's: no Location, PrePivot, Rotation,
  DrawScale or mesh Scale, in the skeleton's units (Engine.dll's
  `execC3DGetJointLocalPos`). A bone that isn't there, or an actor with
  no skeleton, leaves the out values as they were (the original logs
  "Bone doesn't exist").
  - **The axes are in the scripts' order:** X is the skeleton's -Z, Y its
    -X, and Z its Y. This was worked out from what the scripts do with
    them, and fits Kira's skeleton in every case:
    - a head's -Z is where it looks (`Pawn`'s look-at, `CTVE_Pawn`'s
      sight);
    - the eyes, nose and mouth are along -Z and up -X (`NosePos`,
      `CTV_ColdBreath`);
    - a foot standing flat has Z along the ground's normal and X
      toward its toes (`CalcFootZ`).
  The `Rel` forms:
  - `C3DGetJointLocalPosRel` (Engine.dll's) gives the same place as
    `C3DGetJointLocalPos`. Its rotation is the bone's turn from its bind
    pose (`Bind^-1 * Current`, in the skeleton's space), in Unreal's
    axes. "Rel" is relative to the bind pose, not the parent.
  - `C3DGetJointWorldPosRel` (Engine.dll's) gives the same place as
    `C3DGetJointWorldPos`. Its rotation is the bone's turn from its bind
    pose (`Bind^-1 * Current`), carried into the world as `WorldPos`
    carries the bone's own. It's relative to the bind pose too, not the
    parent. A bone that isn't there leaves the out values as they were.
  - No script calls any of the three.
- `C3DDoesBoneExist`.
- `C3DGetAttachPtCoords(Name, out Pos, out Orientation)`: an attachment
  point in the world (`GunTip`, `FirePoint`, `KickPoint`...), as
  Engine.dll's `PointMatrix` places it:
  - It's worked out in the skeleton's space, then placed as the mesh is.
  - On a triangle (type 1): the point's turn and offset in the
    triangle's frame as posed. The frame runs from corner 0, with X
    toward corner 2, Y the normal and Z = X x Y. No bone is involved.
  - On the mesh (type 2): its turn and offset from the skeleton's
    origin, as stored.
  - Of another type, or of kind 7 (the stored matrix's identity flag):
    no turn and no offset.
  - The orientation is in the scripts' order of axes, as the bones'
    (X the point's -Z, Y its -X, Z its Y).
- `C3DGetAttachPtCoordsPrecomputed`: the places worked out in advance
  (`FireGunTipLoc`), the same way as a type 2 point.
- `C3DGetAttachPtOffset`: the point from the mesh's origin, with no
  DrawScale. Its rotation is the turn as the skeleton has it, without
  the swap of axes (as Engine.dll's). Also `C3DGetNumAttachPts`.
- A part the mesh hasn't answers with the actor's own location and
  rotation, so what starts there starts at the actor.
- **Attaching:** `C3DAttach(Obj, ObjPt, ThisPt)` keeps this actor, from
  each tick on, with its point `ThisPt` on `Obj`'s `ObjPt`. It's turned
  so its point's axes are `Obj`'s point's, and moved so the two meet.
  This runs after every actor's tick, until `C3DDetach`. As Engine.dll's:
  - A point not named is the mesh's first. A named point that isn't
    there, or a mesh with no points, ends the attachment.
  - The child's own point is undone in the parent's DrawScale. The
    child's own DrawScale and PrePivot aren't allowed for.
  - What's attached in the levels: the ensigns' tricorders and phasers
    on their belts (`BeltAttach` on `BeltClip`), and the gun nests'
    spotlights.
  - What's attached in play: the flashlight on the wrist, the breather
    on the face, the bat'leth in the hand.
  - The player's guns aren't attached. `UpdateEyeHeight`, which the
    engine sends the player each tick after it moves, calls the game's
    `UpdatePlayerIK`. Its `ApplyWeaponIK` puts the gun in front of her
    chest, aimed where she looks, and sets IK targets for her hands on
    it (below).
- **Before drawing:** the Maps page sends `C3DPreRender` to each mesh it
  is about to draw. The player's weapon then works out its tip
  (`FirePoint`, as `WeaponTipLocation`) and traces the crosshair.
- `C3DActorInit` and `C3DInitAltMeshes` set up The Fallen's renderer, so
  they do nothing here.

**Bending the bones.** The scripts change a pose on top of its
animations. As in Engine.dll, each turn is applied to the pose at once,
on top of the ones before it, and the next tick's pose starts again
from the animations, so the scripts make them anew every tick (they
do: `UpdatePlayerIK`, `UpdateHeadAnimation`). Reluva keeps a tick's turns
in the order made and applies them in that order to the pose for
drawing, for the bone natives and for the attachment points; they
last until the scripts' next tick replaces them:
- **`C3DApplyRotation(Bone, Axis, Angle)`:** degrees about one of the
  bone's axes (in the scripts' order), turning as a rotator's `Pitch`,
  `Yaw` or `Roll` would. In the skeleton's own axes, Pitch is about X,
  Yaw about Y and Roll about Z. A positive Pitch bends the back and nods
  the head forward, which is what the scripts mean when the player looks
  down (`ApplyBackBend`). The upper arms' Roll then swings them back
  against the bend.
- **`C3DApplyWorldRotation(Bone, Rotation)`:** the bone turned where it
  is, its children with it. The turn is taken in the actor's axes, not
  the world's: the scripts build these from the player's own yaw and
  pitch (the hips' and spine's twist in `ApplyBackTwist`, a head turning
  to look at someone in `Pawn`). In the world's axes, a pitch would tip
  the head sideways whenever the actor faced along Y.
- **`C3DRemoveRotation(Bone, bYaw, bPitch, bRoll)`:** takes the bone's
  turn away from its bind pose (the skeleton as modelled) apart into
  yaw, pitch and roll (roll first, then pitch, then yaw, in the
  skeleton's axes), drops those asked for, and puts the rest back: the
  bone where it is, turned from its bind pose by what's left. As in
  Engine.dll, there's no yaw when the bone's X row stands within 0.0001
  of straight up. There's no roll when the cosine between the yawed X
  and the turned X is 0.9999 or more (under about 0.8 degrees).
- **Read from Engine.dll:** `C3DApplyRotation` (degrees, about the
  bone's own axes, before its turn: pitch X, yaw Y, roll Z), and
  `C3DApplyWorldRotation` (the rotator in the mesh's unturned frame,
  through the fixed swap of axes, the actor's own Rotation left out).
  Both match Reluva's. Their turns last until the pose is next worked
  out from the animation, once a tick.
- **The IK chains:** `C3DSetIkTargetPositionAbs` / `C3DSetIkTargetOrientationAbs`
  (in the world), and `C3DSetIkTargetPosition` / `C3DSetIkTargetOrientation`
  (from another actor's place and turn). Each takes a chain's name and a
  weight that blends it in. `C3DClearIkTarget` lets go.
  - Engine.dll keeps the other actor and reads its Location (without
    PrePivot) and Rotation again each time it applies the chains
    (`C3DApplyIK`, 0x1032ac30); Reluva reads them once, when the
    script calls. They differ only for a script that doesn't set it
    again every tick.
  - The chains aren't in the meshes, so the engine must know them by
    name. `LeftArm`/`RightArm` move a hand to a place and
    `LeftHand`/`RightHand` turn it; `LeftLeg`/`RightLeg` and
    `LeftFoot`/`RightFoot` do the same for the feet. Each bends the end
    bone's two parents: upper arm, forearm, hand (`Bip01_R_Hand`), or
    thigh, calf, foot.
  - **What reaches.** With the skeleton's own chains (Male's: every
    character the scripts set IK on), the end bone's origin: the
    chain's effector offset, none on Male's, as Engine.dll has it. A
    skeleton with none keeps Reluva's stand-in: for a hand the mesh's
    attachment point of the same name, for a foot the bone itself.
  - **The solve.** The upper bone turns so the elbow or knee is where the
    two bones' lengths put it on the way to the target, bending in the
    plane it bends in now. The middle bone points at the target. When
    the hand is also turned, the forearm rolls to match it, so the wrist
    keeps the bend the animation gives it.
  - **Correcting for the skin.** A hand's grip point is on skin at the
    wrist that the forearm moves too. So each tick the point is measured
    on the hand once without the IK and once with it, and the solve is
    done again from that. On M04_KiraL1, with the phaser rifle drawn,
    Kira's grip points are 0.5 and 0.0 units from the rifle's, and their
    turns agree to within 10°.
- **`InterpolateRotation(A, B, Alpha)`:** Alpha of the way from A to B,
  the shorter way round, turning evenly: Engine.dll makes the two
  quaternions and slerps them (not each angle on its own). The scripts ease
  the IK turns and the head's look with it. Without it, every foot
  target would come out as turn 0, facing the feet backward.

On M01_KiraL2, standing, Kira's points and bones are:
- head bone 35 above her middle (her top is 47);
- feet 40 below;
- hands at her hips, 14 to 15 units to each side, the right on her
  right;
- `Face` in front of her head.

On M11_KiraL1, an ensign's tricorder hangs upright at her belt. On
M04_KiraL1, once 2 is pressed, Kira holds the phaser rifle at her hip
facing where she faces, both hands on its grips, its muzzle 27 units
ahead of the right hand's.

Checked against Engine.dll (November 2026):
- **The bones' axes.** A bone's place's axes (`C3DGetJointWorldPos`:
  the actor's X is the bone's −Z, Y its −X, Z its +Y, through a fixed
  swap), `C3DApplyRotation` turning about the bone's own X (pitch), Y
  (yaw) and Z (roll) before its local place, and `C3DApplyWorldRotation`
  turning the bone within the skeleton by the rotator carried through
  that swap, the actor's own turn left out (Reluva takes the swap from
  the mesh's placement, the same for The Fallen's meshes): all as
  Reluva has them.
- **The IK, as Engine.dll's** (`C3DApplyIK` and its solver, read November
  2026):
  - **The chains are data.** Shared.u's `Male` keeps eight (the player
    takes them through its alt meshes; only `CTV_Player` sets IK
    targets): RightArm and LeftArm (clavicle to hand), RightLeg and
    LeftLeg (thigh to foot), and a hand's or foot's own. Each names its
    root and end bones, a 4x3 effector matrix in the end bone's axes
    (identity but for RightHand, about 7 degrees), steps of a bone and
    an axis (the arms': forearm Y, upper arm Z, upper arm X, forearm Y,
    clavicle X), 4 iterations and a tolerance of 0.
  - **The bones' joint limits** are the 10 floats after each bone's
    children: two of +-1e38, two more, then min and max about Y, X and Z
    in radians (Male's right forearm 4 to 147 degrees about Y, about 1
    about X; a default of +-30). Read into each bone's record.
  - **The solve:** each step turns its bone about its one axis. The
    bone's turn from its bind pose (`M0 = local x Bind^-1`) is taken as
    Euler angles for `E = Rz Rx Ry`, and that axis's angle is set afresh
    to bring the end bone's origin (the effector: no attachment point)
    round toward the target. It's kept within the bone's limit for that
    axis, and X also within +-90 degrees. Then each of the chain's bones
    is slerped from where it was by the chain's weight. On Sisko and
    Kira holding the rifle, the hands come within 0.02 to 0.3 units of
    their targets.
  - **The hand's or foot's chain** sets that bone's turn outright (the
    wanted axes with the chain's effector turn undone), slerped by its
    weight.
  - **`C3DApplyIKOnBasePose(IkChain, Val)`**: with `Val` not 0, the
    chain's bones go back to their bind pose before the solve. It isn't
    "before the layers": the solve is always after them.
  - A skeleton with no chains of its own or borrowed gets Reluva's own
    two-bone solve. No other script sets IK.
  - **A turn from another actor** (`C3DSetIkTargetOrientation(IkChain,
    Target, RelRotation, ..)`): the native only stores the request (the
    target by name, the rotator, the weight); each frame the engine
    makes the target's `Rotation` into the character's space and
    multiplies the axes of `RelRotation` (`GMath.UnitCoords /
    RelRotation`, rows copied as they are) on before it, row-vector
    style. So `RelRotation` is turned within the target's own axes, as
    Reluva does (`inFrame`): no other axes or order (read at
    0x1032ac30).
  - **The feet** (`CTV_Player.ApplyFeetAlignment`): standing still
    across a step, the player is lowered to the lower foot
    (`FeetPivotZ`, `LocationOffset.Z`) and the higher leg's chain lifts
    its foot back onto the step. Seen on M01_SiskoL1A across the
    20-unit step at (960, 380): lowered by 19.9, the left knee bent with
    its foot on the step, the right leg straight down to the floor.
- Points from the origin are in the skeleton's units
  ([mesh.md](../formats/mesh.md), `reluva-tool unreal points`). With them, Kira holds the phaser rifle at
  her hip, the right hand on its grip at the back and the left under the
  barrel. That needed `Rotator * Rotator` (The Fallen's own operator,
  `Object.uc`'s native 292) to turn the second rotator on in the
  first's frame, as `CTV_Player` turns the player's weapon by its
  `IK_PreRotate`. Multiplied part by part, a weapon with no pre-turn
  would be left at no rotation at all, facing the world's X whichever
  way she faced.

## In Reluva

`vm/vm_attach.cpp` works them out; `reluva-tool unreal points` checks
every mesh's attachment points against its precomputed places.

## Sources and checks

- **Where the facts come from:** Engine.dll's natives, read for what
  they do, where named above; the meshes ([mesh.md](../formats/mesh.md)).
- **How it was checked:** against the meshes' precomputed places
  (`reluva-tool unreal points`), and in play.

## Open questions

- The bone axis order and IK natives are Reluva's own readings until
  compared with the original running.
