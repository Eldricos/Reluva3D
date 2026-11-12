# Mission scripts (GAME.BIN)

> **Status:** decoded · **Code:** `src/engines/stgenerations/formats/script.h`, `src/engines/stgenerations/game/vm.h`

## Overview

This is the compiled script for each location. Everything below comes from `sgens.exe`: the loader at
`0x45F160`, the object factory and code scanner at `0x43519F`, `SendEvent` at
`0x44F4E1`, and the interpreter at `0x44F9D4` (a 245-case switch at `0x455A28`).

## Layout

### Container

```
char[4]  "SGEN"
uint16   22 (version; the loader rejects anything else)
uint16   2
uint32   file size - 12
uint32   code size in bytes
uint32   section sizes[6]
uint16   code[]                  starts at offset 40
uint8    sections[6][]
char[]   resource names, NUL separated, to the end of the file
```

- **Section 0** is a list of `(int16, int16)` pairs ending in −1. The level
  loader passes each pair to a game function (`0x4B5220`).
- **Section 1** is the resource table: 12-byte records, one per name. At load
  time the third field is overwritten with a pointer to the name (the stale
  `0x00424A38`-style values in the file are leftovers).
- **Section 2** is the sound cue table and **section 3** the sound table,
  both handed to the sound manager (`0x41EB62` / `0x41EA4B`); see Sounds
  below.
- **Section 4** holds 100-byte projectile types, ending at a kind of 0 (see Projectiles below).
  Section 5 is unexplored.

### Sounds

Everything a 3D level plays comes from its script's sound table: speech,
effects, ambience and music. Scripts play entries by index. The engine
plays nothing by itself; footsteps, doors and hits are all script handlers.

**Section 3** has 56-byte entries and ends with a key of −1 (verid1 has 342):

| Offset | Meaning |
|--------|---------|
| 0x00 | key: bits 0–15 a number, bits 16–23 a speaker prefix, bit 24 "in COMMON" |
| 0x04 | volume, 0–64 (PLAY_SOUND overwrites it) |
| 0x08 | priority |
| 0x0C | position: 0 none, 1 the point below, 2 a billboard, 3 an actor |
| 0x10 | the billboard or actor (& 0x1FFF) |
| 0x14, 0x18, 0x1C | x, y, z (16.16) |
| 0x20 | radius: silent at this distance and beyond |
| 0x24 | start delay in ms (zeroed at load; set by scripts and cues) |
| 0x28 | repeats: 0 or 1 once, 0x7FFFFFFF forever |
| 0x2C, 0x30 | random pause between repeats, min and max ms (0: seamless loop) |
| 0x34 | flags: 0x1 speech with a subtitle, 0x4 streamed, 0x8 / 0x10 hard left / right, 0x20 higher priority, 0x40 fade in, 0x80 fade out, 0x100 muted |

**File names** (`0x41F8A8`): the prefix, then the number as four digits,
then `.WAV`, or `.TXT` for the subtitle.
- The prefixes come from the table at `0x53FDA8`: MISC PICA TROI GEOR RIKE
  DATA WORF CRUS KIRK ROMU KLIN SORA CHOD HANU EXTR KRAU LURS BETO MUSI TEMP
  COMP.
- The files are in COMMON if bit 24 is set. Music (prefix MUSI) is in the
  MUSIC.1 / MUSIC.2 archives. Everything else is in the mission's `sounds/`
  folder.
- For example, verid1's entry 57 is RIKE0107 and entry 136 is MUSI7501.

**Section 2** holds cues, ended by −1. Each cue is:
- `0x12345678`;
- the cue number;
- flags;
- a count (0 in the file);
- (member, used) pairs, where a member ≥ 0 is a sound and a negative member is a pause of −member ms;
- `0x87654321`.

PLAY_SOUND_CUE (`0x41F4E1`) picks members by the flags:
- **0x20:** all of them in order (with 0x1, only the first time).
- **0x10:** the first member, then a pick.
- **0x4:** the first member the first time, picks after that.
- **0x8:** the last member is kept back until the rest are used up.
- **0x1:** never repeat.
- **0x2:** pick at random (otherwise the first unused).

The picks play one after another, with their pauses in between. Then the
object that asked gets **event 46**.

**Playback:**
- **Channels:** there are 8. A new sound takes the channel of the lowest
  priority, oldest sound.
- **Overlap:** speech lines overlap rather than queue, and each shows its
  subtitle as it starts, replacing the last one.
- **Position:** a positional sound is recalculated every frame (`0x41E583`).
  Its distance d is the largest axis plus a quarter of the other two.
  - At d ≥ radius it is silent. Inside, its volume is 10 + (volume − 10) × (radius − d) / radius.
  - It pans by the direction from the player, less so up close.
  - A one-off positional sound out of range when it starts is dropped.
- **Watchdog:** a sound still playing long after its length (by its length
  again, at least 3 s) is stopped.
- **Subtitles** are shown for the sample's length, at least 2 s. They may
  contain `\T<RRGGBB>\` (colour), `\W<hex>\` (delay), `\B<hex>\`, and
  `\\` for a backslash.

**References.** Natives that take a 32-bit sound reference read it as follows:
- high word 0: a sound;
- high word 0xFFFF: every playing copy of that sound;
- otherwise: the copy whose play call returned that value.

PLAY_SOUND and the fade-in play return such a handle, or −1.

`src/engines/stgenerations/game/sound.h` implements all this.

### Program

```
program := object* 0x1000
object  := type param state+ 0x2000      param is the object's id
state   := (0x8000 | id) block+ 0x3000
block   := event next-state instruction* 0x0000
```

Object types 1, 2, 3, 4, 5, 6, 7 and 8 each create a different C++ class;
verid1 uses types 1, 2, 4, 7 and 8 (178 objects, 658 states, 1010 blocks).
The grammar consumes exactly all 21,969 words of verid1's code.

### Instruction set

Every instruction is one word, except `PUSH n` (2 words), `PUSH32` (3) and
`PUSH_BLOCK` (8). Binary operations compute **top op below**: for example,
`PUSH 10, PUSH 3, SUB` gives −7.

| Op | Name | Effect |
|----|------|--------|
| 00 | END | end of block |
| 01 | NOP | |
| 02 n | PUSH | push n |
| 03 | POP | discard top |
| 04 | PUSH_SELF | push this object's id |
| 05 ×7 | PUSH_BLOCK | push the seven operand words |
| 06 ×2 | PUSH32 | push two operand words |
| 07 | SET_RESULT | pop into the block's result |
| 08 | GOTO_STATE | pop the state to enter when the block ends |
| 09 | SEND_SIGNAL | pop id, pop v; send event 4 with v to object id |
| 0A | CASE | pop c b a; push a; if a == c, go to state b and end the block |
| 0B–10 | IF_EQ/NE/GT/GE/LT/LE | pop b a; push a; if the test fails, skip to the matching ELSE/ENDIF |
| 11 | ENDIF | |
| 12 | ELSE | skip to the matching ENDIF |
| 13 | RETURN | end the block |
| 14 | ALLOC_LOCALS | pop n; allocate n locals |
| 15 | GAME_INIT | pop one value for the game |
| 16 / 17 | GET_LOCAL / GET_GLOBAL | pop i; push the variable |
| 18 / 19 | SET_LOCAL / SET_GLOBAL | pop i, pop v; variable[i] = v |
| 1A / 1B | INC / DEC | |
| 1C–1F | ADD SUB MUL DIV | top op below |
| 20 | SIGN_DIFF | sign(top − below) |
| 21–24 | AND OR XOR NOT | |
| 25 / 26 | HIGH_BITS / LOW_BITS | top & 0xE000 / top & 0x1FFF (object ids look like `type << 13 | n`) |
| 27–F4 | natives | calls into the game (see below) |

The natives' stack effects are in `src/engines/stgenerations/formats/opcodes.cpp`. They were
computed from the stack pointer adjustments in each handler of the
interpreter, where a 32-bit argument takes two slots: the low word is
pushed first, so the high word is on top. Run through every block of
verid1, they never underflow the stack, except in a few blocks that take
more event arguments than the test supplied.

## Behaviour

### Execution

Each object is a state machine. It starts in state −1. When an object
receives an event, the block registered for
`((event & 0xFF) << 8) | (state & 0xFF)` runs. If the object has no such
block, the parent object is searched next, then built-in default behaviour.
When the block finishes, the object switches to the state set by
`GOTO_STATE` or `CASE`, or else to the block's next state (−1 means stay
put). Each state change sends **event 3 (leave)** to the old state, then
**event 2 (enter)** to the new one.

A block runs on its own 16-bit stack. Event arguments are pushed last to
first, so the first argument is on top. The block's result starts at 0 and
is set with `SET_RESULT`.

### Objects and the world

The SGEN loader's object factory (`0x43519F`) makes one C++ object per
script object, its class chosen by the object's type. The top three bits
of an object's id give its type, and for objects that stand for parts of
the level, the rest is the id in the level records' offset 0x14:

| Id bits | Type | Stands for |
|---------|------|------------|
| 0xE000 | 1 | logic: no level record; also behaviour templates other objects inherit from |
| 0x4000 | 2 | walls with that id |
| 0xA000 | 4 | the actor (`GAME.LV2` type 6) with that id |
| 0xC000 | 7 | flats with that id |
| 0x2000 | 8 | billboards with that id |

A native that takes an object id calls the object's own method if the id
is the running object's, and otherwise looks the id up in the mission
object (`0x545EA8`, vtable `0x531768`) and calls the same method on it.
The original doesn't check that the object exists.

### Events

**Delivery.** Events are delivered at once, nested inside the block that
sends them (`0x44F4E1`). An object disabled with `DISABLE_OBJECT`, or still
in state −1, gets no script blocks. Events from the engine are queued
during an engine tick and delivered at its end (`0x4B8120`, `0x4B80D0`).

| Event | Sent when |
|-------|-----------|
| 1 | timer tick (`SET_TIMER_*`); repeats until `KILL_TIMER` |
| 2 / 3 | state entered / left |
| 4 | `SEND_SIGNAL`, with one value |
| 5 | `SEND_SET_VALUE`, with one value |
| 6 | broadcast when the mission begins (`0x43E742`); verid1 places the player then |
| 9 / 10 | broadcast: the player was hurt / healed |
| 14 / 15 | the player started / stopped touching a wall, a billboard's area, an actor, or the floor stood on |
| 16 | the object was hit: damage, power, the attacker's team. Without a handler, an actor not on that team takes the hit (`0x457B29`) |
| 22–25 | a door or lift starts opening, is open, starts closing, is closed |
| 36 / 37 | an actor arrived or finished turning / finished an animation cycle |
| 38–42 | an actor took a hit (`0x45747E`): 38 stunned, 39 noticed only, 40 hurt, 41 knocked out, 42 killed. Power above the full threshold takes damage from hit points and stun points, above the stun threshold from stun points only (`SET_ACTOR_COMBAT_STATS`) |
| 29 / 31 | an actor started / stopped seeing the player: within its near distance and in line of sight (`0x4BB900`: walls and floors in its and the player's zones block, from halfway between its muzzle and its top to the player's eye) |
| 45 | `TRIGGER`, or the player used an item |
| 51 / 52, 53 / 54 | the ceiling / floor above or under the player changed: new / old |
| 55 | a screen effect finished |
| 59, 61 | broadcast: hit report (the object hit or 0, damage, power, team); the player left or landed on the ground |

Broadcasts go only to objects that subscribed with `SUBSCRIBE`. Contacts are
reported on change only: the old thing gets 15 and the new one 14.

**Level start** (`0x4336F0`, `0x4356FF`):
1. Every object is created in state −1.
2. Each object is set to state 0, so it receives event 2.
   - The order is the original's hash map order: bucket `(id >> 4) % 17`
     ascending, and within a bucket the last object in the file first.
   - Events sent to objects that haven't been visited yet reach only the
     built-in defaults.
3. Nothing is sent every frame without a reason. Scripts run on timers,
   contacts and each other's signals.

**Frame time.** The mission clock advances by the frame time, clamped to
8–1000 ms.

### Natives

The away-team locations are a table in sgens.exe (`0x545600`, 15 records of
132 bytes: a game class number, then the folder): AMARGOS1, AMARGOS2, ANTILIOS,
ARVADA1, ARVADA2, BERSUS, ENTERP, EPSION, GUERN, HALEE1, HALEE2, QURASH,
GALORN1, GALORN2, VERID1. The location's game object is built by `0x4331FA`
with the controller as its +0x4C.
The retail game ships twelve of them; ARVADA2, GUERN and HALEE2 are in the table but not on
the discs.

Natives that verid1 uses, by purpose. Arguments are listed as popped, top
of the stack first. A *position* is x, y, z (32-bit 16.16 each) and a
heading in degrees.

- **Objects:**
  - DISABLE_OBJECT, ENABLE_OBJECT
  - SUBSCRIBE, UNSUBSCRIBE
  - SET_PARENT: inherit another object's handlers; they run with SELF unchanged
  - TRIGGER (event 45)
  - SEND_SET_VALUE
  - RANDOM n (0 to n−1)
  - GET_ARRAY, SET_ARRAY: a second array of words that persists across missions
- **Timers:** SET_TIMER_MS, SET_TIMER_SECONDS, SET_TIMER_RANDOM, KILL_TIMER.
- **The level:**
  - HIDE, SHOW: clear or set flag 0x100, which also stops a wall blocking
  - SET_SURFACE_TEXTURE id, texture, frames, period: a frame lasts 4 × period ms
  - RESET_DEFAULT: back to the original texture
  - SET_FULLBRIGHT
  - SET_SKY_ENABLED
  - SET_ZONE_ON_MAP
- **Positions:**
  - SET_POSITION id, position: moves actors; billboards ignore it (`0x4586D0`), and items just settle again
  - GET_POSITION id (0 = the player)
  - SET_HEADING
  - DISTANCE_TO_PLAYER
  - DROP_AT id, where: shows the object and moves it to the player (0) or an actor, items onto the floor there
  - TELEPORT_PLAYER position
  - PUSH_PLAYER
- **The player:**
  - GET_HEALTH, ADD_HEALTH, DAMAGE_PLAYER
  - SET_EYE_HEIGHT, SET_STEP_HEADROOM, SET_PLAYER_WIDTH
  - SET_PLAYER_MOTION (0xB2) speed, turn rate, look rate, acceleration, turn and look acceleration,
    jump (popped in that order; 32-bit 16.16 or words). 0x80000000 / 0x8000 keep a value,
    0x80000001 / 0x8001 restore the default (`0x545334`: 1.5, 15, 10, 1.0, 10, 10, 13.9).
    Cutscenes set speed 0.
  - SET_PLAYER_CONTROLS (0xAE) mask: game+0x1E8, 0x5F by default, tested in `0x43AD04` (through
    the copy at +0x1E4, which is 6 while the player falls outside low gravity: no walking in the
    air): 1 walk and strafe, 2 turn, 4 look up and down, 8 jump (X or keypad .: from the floor, or
    once a second in low gravity), 0x10 crouch (C or Enter: the eye eases to 2.5), 0x20 the view
    bobs while walking (no script sets it), 0x40 in low gravity, walking follows the view's pitch.
  - SET_GRAVITY (0xA3) gravity, fastest fall, step down (16.16; keep / default as above):
    defaults 64.4, 200 and 0.25 (verid1: 80 and 25000). The step down is the drop onto a lower
    floor taken without falling (`0x4B4904`); the original sets it only when the fastest fall
    isn't kept (`0x441257`). LOW_GRAVITY (0xB5): 0, 10 and 0, or the defaults. A fall adds
    gravity · t to the speed, up to the fastest, and moves by speed · t (`0x4B4800`). A jump
    (`0x4B6800`) takes the jump speed (SET_PLAYER_MOTION's last, 13.9) off the fall speed; going
    up, the head stops at the ceiling (event 3).
  - SET_FALL_DAMAGE (0xEE) multiplier, divisor (16.16): landing at fall speed v (event 2), or a
    jump hitting the ceiling at v (event 3), hurts v · multiplier / divisor, damage kind 1
    (`0x4357E5`, `0x43A662`); defaults 1 and 32 (verid1: 47), and keep / default as above. A flat
    marked with SET_FLAT_SAFE (0xED) id, on (its object's +0x6C) does no harm either way.
  - PUSH_PLAYER (0x77) position, strength, tRamp, tHold, tEnd: moves the player towards the point
    (away for a negative strength), ramping up, holding, easing off (`0x43B447`); 0x78 stops it.
  - GET_DIFFICULTY (0xD5): the configuration's +0x340, 1 by default.
- **Actors:**
  - ACTOR_WANDER, ACTOR_APPROACH_PLAYER, ACTOR_ATTACK, ACTOR_FACE, ACTOR_STATE_1/4/5, SHOW, HIDE: set the mode (see Actors)
  - SET_ACTOR_COMBAT_STATS, SET_ACTOR_WEAPON, SET_ACTOR_TEAM, and size and reach settings
  - ACTOR_GO_TO (0x4C) id, x, y, height: mode 7 towards a point (waypoints; event 37 on arrival)
  - SET_ACTOR_ALWAYS_ACTIVE (0x5D) and SET_ACTOR_FLYER (0xEA) id, on: flags 0x400000, 0x1000000
  - SET_ACTOR_CLIMB_ANGLE (0xE8) id, degrees: the share of a flyer's speed going across, +0x80 =
    cos(|degrees|) in 16.16 (`0x456E9C`)
  - RELEASE_ACTOR_CLASS (0xA9): frees a sprite class's frames when unused
  - FIRE_PROJECTILE_AT (0xE2) type, from x, y, height, to x, y, height: a shot from no one
- **Lifts, lights and the world** (from amargos1, arvada1, enterp, galorn1/2, halee1):
  - Script objects come in types: 1 logic (0xExxx), 2 walls (0x4xxx), 3 movers/doors (0x8xxx),
    4 actors (0xAxxx), 5 lifts (0x8xxx), 6 light groups (0x6xxx), 7 flats (0xCxxx), 8 billboards
    (0x2xxx) (`0x43519F`).
  - LIFT_SETUP (0x5E) flat, count, 8 heights (16.16; on the lift itself); LIFT_GO_TO (0x64) id,
    level: every flat of the group jumps to that height (`0x4583EA`). The player isn't carried:
    the car's own panel follows it with TELEPORT_PLAYER_RELATIVE by the same distance.
  - SET_LIGHT_LEVEL (0x66) id, percent: light group id & 0x1FFF; RESET_LIGHT_LEVEL (0x67) id.
  - SET_TEXTURE_SCROLL (0xAB) id, u speed, v speed: the texture moves by
    ((speed · ms) & 0x1FFFF) / 1024 of its 128 units (`0x4B78B0`).
  - SET_FLAT_ZONES (0xC0) id, low, high: the zones in play standing there. ADD_FLAT_ZONE_BAND (0xC1)
    id, height, low, high: zones by height for a lift, the first at or below its height counting.
  - SET_SKY (0xB6) n: sky n, and the sky on. SHOW_VIEW_OVERLAY (0xB8) image, mode: a script
    resource's .OVR over the view, one frame of every row (-1 removes it). The mode is how it is
    drawn (`0x409DDC`): 1 centred (helm.ovr, BIGCROP.OVR), 2 row by row (epsion's SHELL.OVR, the same
    for these 640 x 252 pictures), anything else not at all. MAP_HEIGHT_BAND (0xC9) below, above.
  - ENGAGE (0x73) id: event 35 once the player walks 3 units off (another engaged object gets it at
    once). TELEPORT_PLAYER_RELATIVE (0x76) from, to: moved and turned by their difference.
  - Health: SET_ENVIRONMENT_DAMAGE (0x88) period: 1 health a period (kind 13), 0 stops;
    SET_HEALTH (0x8A); SET_WORN_ITEM (0x8B) id: a kind 9 item (30/30) soaks whole hits while it has
    charge, except kind 12, and gets event 48 when empty; SET_WORN_GAUGE (0xE7); SET_ITEM_CHARGE_MAX
    (0xCB, 0xE6) id, charge, most; GET_ITEM_VALUE (0xCC): a worn item's charge, a weapon's team.
  - TRANSITION_TELEPORT (0x93) position, t1, t2, t3: the game dissolves to the game at the new
    place (see Transitions below), then event 7. SET_TRANSITION_COLOUR (0x94) r, g, b: the
    speckles' colour, the palette's nearest (`0x43E084`; index 0x30 until set).
  - SET_MISSION_OUTCOME (0x97) code: up to five codes for the campaign (story branches).
  - SET_PLAYER_SPIN (0xAF) degrees a second. SET_PLAYER_CONTROLS: see above.
  - Actors: SET_ACTOR_MAP_MARKER (0x40), ACTOR_GO_TO_ACTOR (0x4A), SET_ACTOR_TARGET (0x5B),
    SET_ACTOR_ANIM_SPEED (0x5C, 16.16 stored as 8.8), FIRE_PROJECTILE_AT_OBJECT (0xE3),
    GET_ALERTED_COUNT (0xEF, actors seeing the player now).
  - ACTOR_GO_TO_BILLBOARD (0x4B) id, billboard: mode 7 towards a billboard (`0x456CC9`).
    DISENGAGE (0x74) id: forgets the ENGAGE object if it is this one, without event 35.
    SET_TRANSITION_STYLE (0x95) n: game+0x330 (1 by default), the transitions' speckles: 1 single
    pixels, 2 streaks a sixteenth of the screen wide (epsion uses 2).
  - No effect here: 0xED (a flat's +0x6C, unread), ALLOC_GLOBALS (0x15), the logging natives 0x27-0x2A (0x28 pops only its count
    and logs the words under it), LOAD_ACTOR_CLASS (0xA8), 0xAD, SET_PIXEL_PICK (0xE5).
- **Doors** (movers, `src/engines/stgenerations/game/movers.h`): built at load (`0x4B7CA0`) from walls with a mover id
  and run every tick (`0x4B82E0`). See "Movers" below. MOVER_OPEN (0x39), MOVER_CLOSE (0x3A),
  MOVER_TOGGLE (0x3B), MOVER_LOCK (0x3C), MOVER_UNLOCK (0x3D), MOVER_SET_OPEN (0x3E),
  MOVER_SET_CLOSED (0x3F) id: set the state at once, locked or not (`0x45643E`).
- **Sound** (see Sounds):
  - PLAY_SOUND sound, volume (0–64)
  - PLAY_SOUND_DELAYED, PLAY_SOUND_FADE_IN, PLAY_SOUND_CUE
  - STOP_SOUND, STOP_ONE_SOUND, FADE_OUT_SOUND
  - SET_SOUND_VOLUME, SET_SOUND_LOOP, SET_SOUND_PRIORITY, SET_SOUND_POSITION, SET_SOUND_OBJECT, SET_SOUND_MAX_DISTANCE, MUTE_SOUND
  - PRELOAD_SOUND
- **The mission:**
  - While the mission loads, its BRIEFING.BMP shows; then COMMON.DAT's PROCEED.BMP (83 x 38: the
    pressed look above the normal one, magenta see-through) goes on it at the place the location
    table gives (0x545600, a rectangle per location: AMARGOS1's is 366,188-448,206). Clicking it
    plays PROCEED.WAV (`0x46233F`) and the mission starts.
  - Event 6 starts it. verid1's e001 answers with BRIEFING_TRANSITION (0x91) t1, t2, t3 (32-bit ms,
    1000 each): the briefing gives way to the game (`0x43D8FF`), then event 7 starts the mission
    proper. Its sound is SET_TRANSITION_SOUND (0x96) sound, volume.
  - END_TRANSITION (0x92) t1, t2, t3: the game dissolves to black, then the mission ends
    (`0x43DC37`); ignored while another transition runs. verid1's death (e00b) and its leaving
    (e001 on 46) use it.
  - Transitions (`0x43E13A`, run instead of the game's tick, so the game stands still): three
    phases, each ending at the sum of the times so far. 1: the first picture with speckles of the
    colour growing to a quarter of each row (the display's 0x64, `0x40BC05`, amount
    elapsed·256/t1 in 1024ths: that many random pixels per row; past half it fills with the
    colour and shows that many of the picture's instead). 2: a crossfade to the second picture
    (0x6C, `0x40C852`: random pixels of each row taken from the second, then past half the other
    way round), with a quarter's speckles over it. 3: the second picture with the speckles
    clearing. The briefing goes to the game, the game to black for the ending, and the game
    before a teleport to the game after it.
  - END_MISSION (0xBF): ends it at once (game.vt114 → the controller's `0x443384`, +0x50 = 1).
  - SET_OBJECTIVES (0x99) mask: game+0x94 (-1 until set); OBJECTIVE_DONE (0x9A) bits: game+0x90 |=
    bits; 0x9B clears them. Once they're equal the game broadcasts event 8, once (`0x43AD04`), and
    sets the controller's +0x10: the mission was accomplished. Nothing else says how it went.
  - The player never dies in the engine: damage leaves at least 1 health and broadcasts event 9, and
    the scripts decide (verid1's hypospray steps in at 1 on the easiest setting).
  - GET_ARRAY/SET_ARRAY's words (0x56F844) outlive the mission: verid1 stores the health and the
    phaser's charge there before leaving.
- **Interface:**
  - SET_INVENTORY_ENABLED (0xD6) on; SET_ITEM_ENABLED (0xD7) id, on: greyed out slots
    (grayout.bmp) that ignore clicks
  - SET_ITEM_ICON (0x83) id, icon: the item's INVENT.BMP frame
  - MAP_ZOOM_LEVELS (0xC8): per level a scale and a shift, pixels = units · 65536 · scale >> shift
  - SET_BILLBOARD_UNPICKABLE (0xF4) id, on: flag 0x400, which picking passes over
  - SET_FLAT_GROUPS (0xCD) a, b, c: a pool of water (see Water below). SET_FLAT_FLAG_68 (0xAD) sets
    the flat object's +0x68, which nothing reads (its getter is only reachable by an actor opcode).
  - the close-up panel (PANEL_*)
  - the automap (MAP_*)
  - the inventory and items (INVENTORY_*, ATTACH_ITEM_BEHAVIOUR, ...)
  - screen flashes and tints, view shake

### Projectiles

Section 4's records become projectile types (`0x43FE9C`), numbered from 0;
`SET_ACTOR_WEAPON` gives an actor one to fire in attack mode, and weapon
items name one too. `src/engines/stgenerations/game/projectiles.h`.

| Offset | Meaning |
|--------|---------|
| 0x00 | kind: 1 beam (a head and a trail), 2 bolt, 3 burst (sparks), 4 nothing; 0 ends the table |
| 0x04 | animation rate: frame = age in ms × rate / 65536, wrapped by the frame count |
| 0x08 | lifetime in ms |
| 0x0C | first texture (level texture id) |
| 0x10, 0x14 | 16.16 half width (also the radius it hits with) and height; drawn centred on the shot |
| 0x18 | mirrored view directions |
| 0x1C, 0x20 | keyed (index 255 transparent), see-through |
| 0x24–0x30 | u, v start and length, in 128ths of the texture |
| 0x34, 0x38 | frames, view directions |
| 0x3C | 16.16 speed S: S × 1000 / 65536 units a second (verid1's phaser 106.8) |
| 0x40, 0x44 | fire sound, sounded where the shot starts; impact sound, where it hits; −1 none |
| 0x48, 0x4C | damage, team |
| 0x54, 0x58 | beams: trail sprites and their 16.16 spacing; bursts: sparks and the cone's half angle (1/65536 turns) |
| 0x5C | type thrown where it hits or runs out, or −1 (sparks) |

Shots fly straight without gravity (`0x4C4C70`). Each frame they sweep from
where they were to where they are (`0x4BA9E0`) and stop at the nearest of:
a wall crossed from its front, or either way if it is solid on both sides,
within its height give or take the shot's radius; a floor crossed going
down or a ceiling going up; and, as upright cylinders, billboards
(+0x54 radius, +0x58 high), actors other than the shooter and the player
(radius 1.2 from 6 below the eye to 0.51 above it). Walls, floors and
billboards count only in the zones in play.

A beam strings its trail out behind the head from the muzzle; when the head
hits, the trail runs on into the hit point and vanishes there. Bolts and
beam heads report hits (engine code 20, `0x43696F`): the impact sounds, a
player hit is damage (`game.vt34C`: health falls but never below 1, then
broadcast 9 with source 0, damage, damage, team), and anything else gets
event 16 (damage, damage, team) while everyone hears event 59 (the id,
damage, damage, team). The impact type's sparks fly off what was hit: off
a wall mirrored about it, off a floor with the pitch turned over, off
anything else straight back. Sparks bounce and do no harm.

### The player's attack

Ctrl (or the left mouse button) attacks once per press (`0x43CF95`).

- **With a weapon in use**, the active item (`SET_ACTIVE_ITEM`; items use it
  when the player uses them, event 45), it fires (`0x43FDC4`). Weapon items
  are `ATTACH_ITEM_BEHAVIOUR` kinds 2–6 and 11–13, with c the projectile
  type. They start full (`0x449918`); by kind, charge / highest power level /
  power level: 2 (phaser) 320/16/4, 3 200/12/4, 4 400/16/8, 5 1200/18/18,
  6 240/12/4, 11 and 12 100/10/10, 13 360/9/3. The power level is the
  damage (and power) of the shot and what it costs in charge: setting it
  writes the projectile type's damage (`0x44A03A`), so it holds for every
  item firing that type. `[` and `]` change it by one (`0x44A1B7`,
  `0x44A159`). `ADD_ITEM_CHARGE` keeps the charge within 0 and the most it
  holds; `SET_ITEM_SETTING` is how long the muzzle flashes, in ms
  (`0x449E2C`, verid1's phaser: 300). A shot is broadcast as event 50.
- **Aim.** While the mouse is over the view, weapons aim at it, else at the
  centre (`0x430756`). The shot flies from the gun's muzzle — ahead a, up
  b − 0.5 − 0.375 y / H and to the left c (W/2 − x) / (W/2), for a cursor at
  (x, y) in a W x H view and the item's (a, b, c) — towards the point that
  shows under the cursor, or one W/2 ahead if that is nothing or within the
  player's radius (`0x4B4E10`, `0x4320D1`). The original also raises the
  muzzle as the player looks down, by an amount not worked out. A shot
  never hits the player.
- **The gun held up** (`0x430D19`): the item's overlay, 12 frames of 88 rows
  (each stored bottom up, like every overlay's frames)
  — poses 6–11 (right-low, right-high, centre-high, left-high, left-low,
  centre-low), and 0–5 the same while the muzzle flashes. For a cursor at
  (x, y) its hotspot goes to X = W/2 + 140 (x − W/2) / (W/2),
  Y = A + (B − A) y / B, with A = H − (88 − the highest of the high poses'
  dy) and B = H − (88 − the lowest of the low poses' dy) / 2; below
  H − (lowest dy) − 5 the item can't be used (an arrow cursor). The pose is
  low for the third of the view the cursor is in, or high if the low one
  wouldn't reach the bottom. Overlays, hotspots (dx, dy for poses 6–11) and
  muzzles (a, b, c), from `0x5428D8` and `0x542A30` on:

  | Kind | Overlay | Hotspots | Muzzle |
  |------|---------|----------|--------|
  | 2 | nuphaser.ovr | 116,34 116,3 75,0 35,2 35,33 74,29 | 1.5, 0, 1 |
  | 3 | nudisrup.ovr | 110,34 110,3 75,0 41,2 41,33 74,31 | 1.5, 0, 1.0625 |
  | 4 | nudrifle.ovr | 94,18 92,8 73,0 48,8 48,19 73,14 | 1.5, 0, 1 |
  | 6 | nuklidis.ovr | 100,33 111,2 77,0 46,3 55,34 77,30 | 1.5, 0.09375, 1.1875 |
  | 11 | nuplant.ovr | 119,33 120,11 75,4 28,12 28,32 75,26 | 1.5, 0.125, 1.5 |
  | 12 | nuplant2.ovr | as 11 | 1.5, 0.125, 1.5 |
- **Otherwise** the player punches (`SET_PUNCH`: overlay, ms per frame, hit
  frame, damage, power, swing sound and volume, hit sound and volume). The
  overlay plays over the view, at most a frame a tick, and on the hit frame
  the blow lands on what the centre of the view shows (`0x43A39F`, code 19,
  `0x4364B8`): within the player's radius plus half a unit, plus the radius
  of a billboard or actor, it gets event 16 (damage, power, team 2) and the
  hit sounds. Event 59 reports the hit, or 0 for something out of reach.

### The away-team screen

The 3D view is the top 640 x 252 (the original renders 640 x 320 and shows
rows 34-285, the same picture; a narrower 504-wide view with borderl/borderr
.bmp is also possible). The console below, at (0,252)-(639,479), is drawn
from bitmaps: plain names from COMMON.DAT, the mission's from its folder.
Each bitmap has its own palette; its colours are drawn in their nearest
matches in the level's palette, as Windows did when blitting a DIB to the
8-bit screen. Frame n of a strip is the n-th from the bottom (`0x40390E`):
so 3health.bmp's frame 2, for health above 15, is the top one.
Some are drawn see-through (image panels with their flag set, `0x425A00`):
pixels matching the first pixel in the file, its bottom left, are skipped —
INVENT.BMP's purple (index 59), P-ARROW.BMP's red (159), 3HEALTH.BMP's
magenta (79). Others are opaque: UNITHLT0.BMP is a single colour. The
bitmap class's default key, before a panel asks for that, is 0xFE
(`0x4039CE`).
Rectangles are inclusive. `src/engines/stgenerations/game/hudscreen.h`, `hud.h`.

| Part | Where | What |
|------|-------|------|
| left panel | (0,253)-(205,458) | l-m-m-o.bmp, frames of 206 rows by mode: off, object, map, life; tabs at (6,261), (6,331), (6,401), 23 x 52 |
| life | (40,269), (118,393) | body picture (`SET_BODY_IMAGE`, -1 = BODY.BMP, frames of 177 rows); health: 3health.bmp (frames of 50 rows: over 15, over 5, else), then unithlt0.bmp units at x 119, unit k's top at 442 - h - 1 - 3k (- 7 from the 16th) |
| map | (39,267)-(192,414) | walls flagged 0x800 in the height band shown, markers (map\dot-*.bmp, 7 x 7, four frames; dot-enrg and dot-tril for energy and trilithium), the player (agpface2.bmp, 8 headings); buttons of 27-row frames: levels.bmp (43,415) and zoomz.bmp (74,415), three frames for the band and zoom; s-life, s-energ, s-lith.bmp at (117,415), (141,415), (165,415), dim (0) or lit (1), filtering the markers |
| object | (39,267) | the mission's OBJECT.BMP |
| right | (206,252)-(639,479) | actionx.bmp |
| weapon display | (222,252)-(431,296) | weapphas.bmp for the phaser, p-arrow.bmp at x 222 + 11 level − 4 (clicking sets level (x − 239) / 11 + 1), and a gauge of 59 ticks 3 apart whose empty part, 59 − ⌈59 charge / most⌉ ticks, is wiped in colour 0 from (413,280) leftwards, 2 x 7 each; wepcycle.bmp at (207,253) picks the next weapon |
| close-up viewer | (223,311)-(433,458) | the visible panel (frames of 148 rows) and its buttons, or the mission's AUXDEF.BMP; auxcycle.bmp at (207,425) shows the next panel, blinking while there are several |
| inventory | (435,256)-(639,457) | 3 x 4 slots, icons at x 436, 505, 574 and y 257, 308, 359, 410: frame b (of `ATTACH_ITEM_BEHAVIOUR`) of the mission's INVENT.BMP, 64 x 46; the item in use framed in palette index 0x22 |
| text | (10,461)-(639,477) | subtitles, Fonts\Action.fnt |

The painter sets wall flag 0x800 on every wall it draws (`0x4C8602`), so the
automap shows what has been seen; `SET_ZONE_ON_MAP` clears or sets it by zone.
Clicking an inventory slot (press and release on it) makes an item the
player can hold the one in use and sends it event 45; a right click sends 62.
Moving 4 pixels with the button down drags the item (not slot 0's): dropped
on a slot of 1-11 it goes there and what was there to the first free slot,
dropped elsewhere in the inventory to the first free slot, and dropped
outside it leaves the inventory and gets event 63 (`0x44763A`). Clicking the close-up viewer
sends 45 (no buttons) or 47 with the button hit (0 for none), a right click
62, and Space sends 45; the panel's object gets 35 every frame once the
player is 2 units from where it opened. Screen effects blend the palette:
`c = (c (128 - k) + colour k) >> 7` per slot, slot 3 first and slot 0 last.

### Movers (doors)

Doors are built from fields of the wall records (152 bytes) and flat records (292 bytes):

| record | offset | meaning |
|--------|--------|---------|
| wall | 0x8A | mover id (0 for none) |
| wall | 0x8C | mode: how the part moves; also ties followers to a leader |
| wall | 0x8E | speed: the mover's rate is speed · 64 / 100 (default 0x4000) |
| wall | 0x90 | wait: the door closes by itself after wait · 4 ms (default 0x100, only if set) |
| wall | 0x92 | the mover walking into this wall starts (`0x4B94D0`) |
| wall | 0x94 | how: 0 open if shut and not locked (every file), 1 open the other way, 2 close, 3 toggle |
| wall | 0x6C | flag 0x2000: a follower part; 0x80000: reverses when it meets the player |
| flat | 0x106, 0x108 | mover id and mode (6 or 7), for flats with flag 0x2000 |

Walls with a mover id and no 0x2000 are the mover's leaders; followers (walls and flats with 0x2000)
go with the leader of the same mover whose mode is theirs. A mover's position runs from 0 (shut) to
±0x4000, by rate · ms >> 10 a tick. Every part is placed from its original geometry with
f = position / 0x4000, A and B the leader's ends and H its height:

| mode | the part |
|------|----------|
| 0, 1 | slides by f · (B − A) towards A, or B |
| 2, 3 | swings by f · 90° about A, one way or the other |
| 4, 5 | swings about B |
| 6 | slides up: the leader's bottom rises by f · H (its v length shrinking); followers' tops rise; flats rise by f · H |
| 7 | slides down: the leader's top sinks, its texture with it; followers' bottoms sink; flats sink |

Moved walls take normals recomputed as (dy, −dx) / length, distance −(n · v2), as every wall in the
files has. States: 0 idle, 1 open, 2 opening, 3 closed, 4 closing, 5–7 the same the other way. Open
movers with a wait count it down and close; changes send the script object 0x8000 | id event 22
(opening), 23 (open), 24 (closing) or 25 (closed); doors' scripts play their own sounds. A step that
brings a part nearer the player once within 1 unit, or a ceiling down onto their head, is undone, and
a closing door that reverses opens again. The player rides floors that move. Actors aren't checked.

## In Reluva

`src/engines/stgenerations/game/mission.h` runs a level's scripts. Every native the eleven
away missions use has a handler. Anything else balances the stack and is logged once with the
developer settings' console output about the missions' scripts
(`debug_channels=scripts` in reluva.ini, or `reluva-tool stgenerations play
... --debug 1 --channels scripts`); at level 2 it also logs every event sent,
with its signal value and the target's state.

To get a full listing of a script:

```sh
reluva-tool stgenerations extract DATA/VERID1.DAT out 'verid1/game.bin'
reluva-tool stgenerations script out/verid1/game.bin
```

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
