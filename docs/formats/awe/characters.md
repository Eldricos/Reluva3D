# Characters

This page covers how *Murder on the Orient Express* animates its
characters (`NPCX`, and `PCXX` for the player):

- what the script verbs `anim`, `turn`, `look`, `rotate`, `animate`,
  `pause`, `resume`, `attach` and `detach` do;
- the behaviour behind those verbs: blinking, idle gestures and talking;
- what they leave outdoors: footprints in the snow, and their breath.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the character's animated model, `0x403e10..0x405940`;
- the model instance's Granny controls, `0x4275d0..0x42aad7`: loading an
  animation and its mask (0x4297d0), playing one on the body (0x429f50,
  with the pose's mask) or on the head or eye layer (0x42a650), stopping
  one (0x429cc0, 0x429dd0). The Granny functions they call were named
  from `motoe.exe`'s imports;
- the turns, `0x4411f0..0x4420b0`;
- the runner's handlers for the verbs;
- outdoors: the character's update (`0x43ffa0`) and walk (`0x4422c0`),
  the footprints' laying and fading (`0x43fda0`, `0x43fd00`), the
  `FOOT` class (`0x40dda0`), the model's update with its breath
  (`0x425f50`), the `BRTH` class (`0x406840`), and how the model draws
  with a diffuse alpha under 1 (`0x428b1f`).

The findings were checked against the install's models, their animation
files' track masks, and the scenes' and dialogue's scripts.

## When a character is there

A character is an object like any other: drawn, shadow and all, only
while it's shown, that is not hidden by a script and its `condition`
holding. The character's draw (`0x425e00`, slot 0xc of the `NPCX` and
`PCXX` vtables) asks the object's shown test (slot 0x50, `0x40ecd0`:
+0xb00 and +0xb04) first and draws nothing if it fails; the common
update (`0x40e900`, slot 0x4c, shared by every object class) tests the
condition every frame and shows or hides the object by it. 49 characters
in the install have a condition: Greta Ohlsson, for one, stands in the
Sirkeci market's scenes only with `tag gretafollow`, and Michel in the
corridors only until `tag micheladmonish`. The pick's list uses the same
test ([ac2.md](ac2.md#the-pick-0x435eb0)).

## Animations on Granny controls

A character plays up to **ten** animations at once, each on a Granny
control. Every animation is `models\<who>\<who>_<name>.gr2`.

Each animation belongs to one of three layers:

- **The body.** The animations' local poses are blended bone by bone.
  Each bone's weight is the animation's current weight times the bone's
  **track mask**.
- **The head** (`headleft`, `headright`...).
- **The eyes** (`eyeleft`, `eyeright`).

The head and eye layers are laid over the body's pose. Each bone moves
toward the layer's pose by the mask of the layer's strongest animation,
times how far that animation has eased in. That amount is its weight
divided by its ease time, capped at 1.

### Track masks

The track mask is in each animation file's skeleton, as the bones'
extended data `TrackMasktrackmask`. Only one bone of an animation carries
a weight:

| Animation | Weighted bone |
|-----------|---------------|
| `idle1`, `walk` | `Bip01` at 0.01 |
| gestures (`idle2`) | `Bip01 Spine` at 0.99 (0.1 for some, like `agree`) |
| `headleft` | `Bip01 Head` at 0.99 |
| `eyeleft` | the eyes at 0.99 |
| `blink` | the eyelids at 0.99 |
| `talk` | `Jaw_Bones_Master` at 0.99 |

The mask is built a bone at a time from the root (0x42a2d4):

1. A bone with a weight in the file keeps it, or takes the script's
   weight if it gives one (`anim fanger .02`).
2. A bone without one takes its parent's, if that is above 0. The jaw's
   and eyes' masters (`jaw_bones_master`, `eye_bones_master`, in any case)
   are the exception: they inherit only a parent's weight of exactly 0.01.
   So a gesture's 0.99 on the spine leaves the face free.

A gesture's 0.99 against a base animation's 0.01 means the gesture moves
the bones it weighs, and the base animation moves the rest. At 0.02
against 0.01, the result is about two thirds the gesture.

The game asks for the mask `TrackMaskTrackMask`. The files spell it
`TrackMasktrackmask` or `TrackMaskTrackMask`, so Granny matches the name
whatever its case. An animation without a mask (57 files) plays on every
bone in full (0x4298f0 logs "No track mask data", and the control gets no
mask). Other masks some animation files carry (`TrackMasksitidle1`) are
never asked for.

### The pose's mask: sitting

MODL's `anim` names a mask of the character's **model** file, and MODL's
condition says when it applies. In the data `anim` is always `sit`, on
the seated characters. Their models carry `TrackMasksit`: `Bip01 Spine`
at 1, so the upper body.

1. **Built with each base animation** (0x429f96). When a base animation
   plays, the last pose mask is freed. If `anim` isn't empty and MODL's
   condition holds, `TrackMask` + `anim` is read from the model's
   skeleton: each bone without a weight takes its parent's, if above 0
   (0x42a08c). If the model has no such mask, there is none.
2. **Applied to everything else.** Every animation that isn't a base one
   then has its mask multiplied by it, bone by bone: gestures (0x42a3da),
   and the head and eyes (0x42a88a). The base animation itself is left as
   it is.

So whatever plays over a seated character's base moves at most its spine
and what hangs from it; the hips and legs stay in the seated pose. Most
gestures weigh only the spine anyway, but an animation weighted on
`Bip01`, or one without a mask, would otherwise move the whole body.

### Base animations and overlays

A **base** animation is a continuous full-body one: `idle1` and its
variants (not `talkidle1`), `walk`, `run`, `sit...`, `eaves...`, and the
`rotate...` animations. A new base animation eases the previous one out.
Everything else is an **overlay** that plays over the base.

- **Ease in.** The script's value is used if it gives one. Otherwise base
  animations take twice the ANIM part's talk ease (0.3 s), and overlays
  take a third of their length, at most 2 s.
- **Ease out.** A finite animation eases out over its last ease time.
  Then "ending" fires:
  - a base animation hands over to `idle1`, or `eavesidle` after
    `eavesstart`;
  - a talking idle's variation hands back to `talkidle1`.
- **The end.** When an animation ends, an `anim` waiting on it is done.
  An animation stopped by another one ending it early doesn't report.

### Which file plays

1. A **renamed animation**: in the ANIM part's list, an entry whose name
   matches and whose condition holds names the file to play.
2. The **MODL prefix**: if `anim2` is set (`sit`) and MODL's condition
   holds, `sit<name>` plays if that file exists. So a seated character's
   `idle1` is `sitidle1`, and its `talkidle1` is `sittalkidle1`.
3. Otherwise, the name itself.

## The verbs

| Verb | Arguments | What it does | Waits |
|------|-----------|--------------|-------|
| `anim`, `anima` | name, weight (0: the mask's), loops (1; 0 for ever), ease | Finite overlays fade over 0.3 s, then the animation plays. | `anim`: until it ends |
| `turn` | a heading in degrees relative to the body (more is to the character's left), someone or something, a cell `a b`, `home`, `home all`, `up`/`down` [weight] | Turns the head. See below. | no |
| `look` | `left`, `right`, `home`; a strength (1) | `eye<dir>` on the eye layer, looping, at that strength. `home` eases the eyes back. | no |
| `rotate`, `rotatea` | an absolute heading in degrees, someone, a cell, `home`, `sp<n>`, `-sp<n>`, `camera` | Turns the body. See below. | `rotate`: until it's there |
| `animate` | seconds | Moves the animations' clocks on: the door macro skips the door knob's animation to its end behind a fade. | no |
| `pause`, `resume` | character | Stops the character's routine, with its walk or turn, and plays `idle1`. `resume` restarts the interrupted event. A character stays paused when its scene is left and shown again (the game keeps the scene; Reluva keeps the flag with its position): Klaus is paused for the cleaver talk, through its close-ups, until the script resumes him. | no |
| `loop` | | In a routine: queues the routine again. | no |
| `attach`, `detach` | object | Attaches or detaches `models\objects\<name>_model.gr2`. The prop has its own skeleton, and the character's animations move its bones too, so it isn't fixed to a bone. | no |
| `hideshadow`, `showshadow` | | The character's shadow flag. | no |

### Headings

Headings are in degrees, the game's way. 0 faces +Z, and the heading
grows as the character turns to its own left. A model at heading θ faces
(sin θ, 0, cos θ).

- The heading toward a point is `atan2(dx, dz)`.
- A walk point holds the heading of someone arriving through its door,
  so `rotate -sp2` (the point's heading plus 180°) faces the door.

### `turn`: the head

1. If the target is within 2° of the body's heading, nothing happens.
2. Otherwise the shorter way is chosen. `Δ` is the angle that way, capped
   at 45°.
3. `headleft` or `headright` plays on the head layer, looping, eased in
   over 0.5 s, at weight `Δ/90`. With `up` or `down` it is
   `headleftup`, `headrightdown` and so on.
4. The turn holds until the next turn. `turn home` eases it out.

`turn up` or `turn down` on its own plays `headup` or `headdown` at
weight 0.5. Only one character has those files.

### `rotate`: the body

1. If the target is within 2°, the event ends at once.
2. Otherwise the shorter way is chosen; a tie goes right.
3. The turning animation depends on how far: `rotatel45`, `rotatel90` or
   `rotatel180` (`rotater...` for the other way) for up to 50°, up to
   100° and more. Characters without the sized sets play `rotatel` or
   `rotater`.
4. The body turns at the animation's own pace: as much each frame as the
   animation's root turns. A character with no turning animation turns at
   200°/s.
5. Within 2° of the target, the heading is set, `idle1` plays, and the
   waiting script goes on.

## Behaviour of their own

The ANIM part's settings, with Debenham's values as an example:

| Setting | Debenham |
|---------|----------|
| talk ease | 0.15 |
| blink interval (least, most) | 5, 8 |
| idle variation interval (least, most) | 8, 12 |
| chances of variations 2, 3 and 4+ | 50, 30, 20 % |

### Blinking

Every 5 to 7 s, the character blinks once or twice.

### Idle variations

The interval is redrawn each time, and after every gesture. It is 2 s
shorter while a talking animation plays and 2 s longer otherwise.

When it runs out:

- **While talking** (`talkidle1` plays): `talkidle1` gives way to
  `talkidle2`, `talkidle3` or a later one, by the chances above.
- **Otherwise**: if `idle1` plays, no conversation is on screen, and no
  other variation or door-knob animation plays, then `idle2`, `idle3` or
  a later one plays once over `idle1`.

### Routines

A non-player character can have an `IDLE` list of conditional scripts.
At entry, the first one whose condition holds runs on the character's own
script runner, and it usually ends with `loop`. For example:

```
anim think, wait 2, anim upset01, turn -40, wait 2, turn home, wait 3, loop
```

The main script resumes the routine of a character it was started from
(clicked) once it's over.

### Followers

A character whose `FLLW` is non-zero follows the player while its `COND`
holds. In the data that is Michel at Belgrade and Greta Ohlsson at
Sirkeci. A follower behaves like this (`0x4400a0`):

- **Arriving.** It is placed by the scene's walk point and walks in,
  like the player.
- **Following.** While the player walks somewhere, a follower that is
  idle, 250 units or more away and further from the goal than the player
  walks there by the path-finder. It stops 125 units short (`0x442780`),
  and runs when the player runs.

### Talking

Over the course of a line in a conversation:

1. When the line starts, finite gestures fade and `talkidle1` plays
   (`sittalkidle1` for a seated character).
2. The mouth (`talk`, on the jaw) plays while the voice is loud, and eases
   out over 0.15 s when it's quiet. With no recording, it plays for the
   whole line.
3. When the line ends, the mouth and the talking idles ease out.

The line's Target turns the speaker's head as `turn` would. When the box
slides away, the heads turn home.

### The face

The face's bones hang from two masters, `Eye_Bones_Master` (eyes, lids,
brows) and `Jaw_Bones_Master` (jaw, lips). The game has no code of its
own for them: they move only by animations on Granny controls, like the
body's, each weighing its bones through its track mask. The only face
animations `motoe.exe` names are `blink`, `talk` and `eye...`
(`head...` turns the head). Four things play them:

| What | Animation | Mask | Played by |
|------|-----------|------|-----------|
| blinking | `blink`, once or twice, eased in over 0.01 s | the eyelids at 0.99 | the character's own timer (0x404210, 0x404670) |
| talking | `talk`, for ever | `Jaw_Bones_Master` at 0.99 | the dialogue's lip sync (0x4046c0, 0x4046f0) |
| the eyes | `eyeleft`, `eyeright`, on the eye layer | the eyes at 0.99 | `look` |
| expressions | `f<name>`: `fanger`, `famused`, `fhappiness`, `fshock`... | mostly `Eye_Bones_Master` at 0.99 | scripts' `anim` |

`blink` and `talk` go through the same steps as a script's `anim`
(0x404b00): a renamed file or the MODL prefix would apply, though the
data has neither for them.

The expressions are ordinary finite overlays. About 400 `anim f...`
commands ask for them, nearly all in the dialogue's event files (`anim
fanger`, sometimes lightened: `anim fanger .02`). Like any `anim`, one
fades the finite gestures playing (and a blink) first, eases in over a
third of its length, and ends. Of the 204 expression files, 196 weigh
`Eye_Bones_Master` (the eyes, lids and brows); the others weigh the face's
bones as a whole or the jaw. Three kinds are never asked for, by a script
or by the code: `fidle` (Poirot's, Antoinette's), `ftalk` and
`flookright` (Antoinette's). Some models also carry a `TrackMaskhead`,
which nothing reads.

## Outdoors: footprints and breath

Two `MODL` fields ([ac2.md](ac2.md)) are for the snow. The player has
them in the snowy exteriors.

### Footprints

A character with `footprints`, in a scene marked `OUTD`, leaves prints
as it walks (or runs) along a path (`0x4422c0` calls `0x43fda0` with the
frame's time):

- It keeps a time into its stride (+0x14f4) and the foot next down
  (+0x14f8), from 0 when it's made; they carry on from one walk to the
  next. The right foot comes down 0.45 s into the stride, the left at
  0.9 s, and at 1.16666 s the stride starts again (that time taken off,
  no print).
- A print is a new object in the scene: a `FOOT`, a model like a
  character's but casting no shadow (`0x40dda0`), with the models'
  defaults (diffuse 1, ambient 0.5, lit). Its model is
  `models\footprint\snowprint_model.gr2` (a snowshoe's, 130 × 137 units)
  once the story tag `snowshoeson` exists, else `footprint_model.gr2` (a
  shoe's, 32 × 68). It stands where the foot's bone (`Bip01 R Foot`,
  `Bip01 L Foot`) is, on the floor (y 0), turned as the character is.
- Each lasts 5 s (`0x43ff73`). Its diffuse alpha (+0x98) fades it: twice
  its age for the first half second, then (5 − age) × 2/9 (`0x43fd00`).
  At 5 s it's taken out of the scene. The ages go on in the character's
  update, while the scene is `OUTD` (`0x43ffbd`).
- A model whose diffuse alpha is under 1 is drawn blended: source alpha,
  one minus source alpha, its alpha the texture's times the diffuse's
  (`0x428b1f`). The prints' textures are a flat colour whose alpha
  draws the print, at most about 0.4, so they're faint.
- Like every object, a print is drawn among the others by its depth
  (its z), and the pick sees it (it has no rectangle or outline, so
  nothing picks it).

### Breath

A character with `breath`, while it's shown, has a `BRTH` particle
system of its own (+0xe3c), made the first time (`0x425fd0`) and
deleted when `breath` is 0 or the character hidden (`0x4260e8`). Each
frame (`0x425f50`) its emitter goes to the bone `Upper Lip_R`, and its
velocity becomes (0, 0, 50) turned by the character's rotation
(`D3DXMatrixRotationYawPitchRoll`, `D3DXVec3TransformCoord`): 50 units
a second the way it faces. It's then updated as the scenes' particles
are ([ac2.md](ac2.md), `SYST`), and drawn just after the character
(`0x425e48`).

`BRTH`'s constructor (`0x406840`) sets, over the particles' defaults:

| Setting | Value |
|---------|-------|
| texture | `Elements\Particles\breath01.tga` |
| blended by alpha | yes |
| velocity | (50, 0, 50), then as above |
| burst, interval | 1 every 0.3 s |
| spread, copies | 1, 1 |
| size | 100 |
| life | 1.5 s, faded in and out over 0.75 s |
| mode | 0 (thrown), no gravity, no bounce |
| blink | on 2 s, off 2 s: a breath at a time |
| sway, swaySpeed | 15, 0 |

So a puff leaves the mouth every 0.3 s for two seconds in four, and
drifts at up to 35 units a second, fading in and out over a second and
a half. A thrown particle's speed on each axis is a random part (−0.71
to 0.71) of the velocity's, so the puffs go along the line the
character faces, forward or back, rather than straight ahead.

## In Reluva

`Animator` (`src/engines/awe/animator.cpp`) handles the animations: the
ten slots, the three layers, the masks, eases and ends. `Actor`
(`actor.cpp`) holds a character's behaviour and verbs. The Scenes page
gives each character an Actor, runs its routine, and passes the verbs and
the dialogue's talking to it.

The Actor sets the pose's mask (`Animator::setPoseMask`) as each base
animation plays; the Animator multiplies it into the masks of what plays
after. A prop's bones take the weights of the character's bones of the
same name. Each slot's curves are evaluated as Granny samples a control,
wrapping round at the ends while it loops (see the Granny page,
"Evaluating curves").

Outdoors (`scene_outdoors.cpp`), each character keeps its footprints,
its stride and its breath. The prints are drawn as models among the
pictures by depth, as the characters are; the breath's particles are
drawn on the 2D layer just after the character, with the scenes' other
particles' code.

There are two deliberate differences from the game:

- An `anim` whose file is missing ends at once. In the game it waits for
  ever.
- An `anim` finishes whether its animation played under the asked name or
  a renamed one.

Settled since:

- **`headleft` and `headright` at full weight.** In every character's
  files they turn the head 90° either way, so 180° apart. The head's
  weight for a turn is therefore its angle ÷ 90.
- **Granny's ease curve.** The game always calls
  `GrannyEaseControlIn(control, seconds, 0)` (0x42a523 and three
  others), so the ease never starts from the current weight.
  - granny2.dll keeps the curve as four bytes, each ÷ 255. For an
    ease-in from weight m these are (m, m, 1, 1).
  - It evaluates the curve as a cubic Bernstein polynomial, with t
    clamped to [0, 1]. From 0, that is exactly smoothstep, 3t² − 2t³,
    which is what Reluva uses.
  - An ease-out goes from the weight m it starts at, as m·(1 − smoothstep).
    Reluva multiplies the ease-in's weight by the ease-out's (1 − smoothstep)
    instead. The two differ only when an ease-out begins before the
    ease-in has finished.

- **The shadows.** Checked against a recording of the game: the
  darkening, and the shadow's direction and length, as Reluva draws
  them (see [shadows.md](shadows.md)).
