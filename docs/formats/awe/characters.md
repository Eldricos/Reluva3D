# Characters

This page covers how *Murder on the Orient Express* animates its
characters (`NPCX`, and `PCXX` for the player):

- what the script verbs `anim`, `turn`, `look`, `rotate`, `animate`,
  `pause`, `resume`, `attach` and `detach` do;
- the behaviour behind those verbs: blinking, idle gestures and talking.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the character's animated model, `0x403e10..0x405940`;
- the model instance's Granny controls, `0x4275d0..0x42a650`;
- the turns, `0x4411f0..0x4420b0`;
- the runner's handlers for the verbs.

The findings were checked against the install's models, their animation
files' track masks, and the scenes' scripts.

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

A bone without a weight of its own takes its parent's, except the jaw's
and eyes' masters. Those two inherit only from a parent weighted 0.01, so
a gesture's 0.99 on the spine leaves the face free.

A gesture's 0.99 against a base animation's 0.01 means the gesture moves
the bones it weighs, and the base animation moves the rest. When a script
gives a weight (`anim fanger .02`), it replaces the mask's weights. At
0.02 against 0.01, the result is about two thirds the gesture.

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
| `pause`, `resume` | character | Stops the character's routine, with its walk or turn, and plays `idle1`. `resume` restarts the interrupted event. | no |
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

## In Reluva

`Animator` (`src/engines/awe/animator.cpp`) handles the animations: the
ten slots, the three layers, the masks, eases and ends. `Actor`
(`actor.cpp`) holds a character's behaviour and verbs. The Scenes page
gives each character an Actor, runs its routine, and passes the verbs and
the dialogue's talking to it.

There are two deliberate differences from the game:

- An `anim` whose file is missing ends at once. In the game it waits for
  ever.
- An `anim` finishes whether its animation played under the asked name or
  a renamed one.

To check:

- the exact angle of `headleft` at full weight;
- Granny's own ease curve (here a smoothstep);
- the sit mask (`TrackMask<anim>`), which isn't applied yet;
- walking in through a scene's flag;
- the shadows.
