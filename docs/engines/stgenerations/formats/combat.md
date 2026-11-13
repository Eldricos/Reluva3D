# Star Trek: Generations: space combat (`COMBAT.DAT`)

> **Status:** partial · **Code:** `src/engines/stgenerations/formats/combatdata.h`, `src/engines/stgenerations/game/combat.h` · **Checked on:** the 2 CD Windows release: `COMBAT.DAT` read whole, with the other archives, and its 27 briefing and console bitmaps decoded; the first fight played · **Confidence:** layout confirmed; behaviour from the code unless marked

## Overview

Space combat's files, in `COMBAT.DAT`.

## Layout

| File | Format |
|------|--------|
| `.3DV`, `.3DR`, `.PCn` | DRE object v1.10: parts of triangles with a material, 16.16 UVs, face normals; x across, y up, z forward. A face's material has a flag in its top bit (the shields, the debris, the phaser beam), taken here as "both sides shown" |
| `.MTL`, `.MTR`, `.DMG` | 36-byte materials: RGB555 colour, a parameter (4096 for the shield dome, 11000 for the shield spheres, else 0), flags (1 textured, 2 effect, 3 flat, 0 the spheres' flat see-through), texture name |
| `.IMG` | GIF87a textures, each with its own palette |
| `.LBM` | IFF PBM effect textures |
| `.RAS` | Sun rasterfiles: the console; colour (255,0,163) is see-through |
| `.SPR` | animations ('SPRT', 'TIME', 'COMP' frames packed four ways) in `STANDARD.PAL` (6-bit) |
| `.MET` | big-endian glyph cells into `SCANNER.RAS` / `A-Z_0-9.RAS` |
| `SYSTEMS.DAT` | ship classes: speed, agility, 27 systems (shields 1-4, weapons, life support, engines, power, computer, sensors, damage control, cloak, 10 phaser banks with firing octants, 4 photon tubes), the shields' leak tables, phaser and photon range and damage |
| `SCn.MIS` | `SpaceCombat n`: `:wave` and ship lines `id type x y z yaw pitch velocity AI ...` (x, y the plane, z altitude; yaw in degrees from +y towards -x, so 90 faces -x; AI `soran`, `quarash` or `standard-N` to aim at system N) |
| `COMBATn.BMP`, `PROCEED.BMP` | the briefing; PROCEED's position is a table in the exe (0x548618) |

### TACTICS (`*.BIN`): not used

COMBAT.DAT's eleven `.BIN` files (KLINGON.BIN, WARBIRD.BIN, CHODAK.BIN, ...)
are compiled AI scripts, and sgens.exe still has their interpreter
(`0x4B0000`-`0x4B4630`). But only `0x4A40F2` attaches one, and the mission
loader always passes it no name, so none is ever loaded. The format, for the
record:

- "TACTICS", then a header to 0x6B (a version, sizes, "MAIN").
- Tokens: u32 kind, u32 code, u32 size, then size bytes.
  - Kind 3 is a page: the code that follows, named at +8 ("KERNEL",
    "ATTACK_SET", "SMALL_LOOP", ...). Branches name pages by file offset.
  - Kind 0 is "when": a condition (table at `0x550BC8`; 14, 23, 28, 29 and
    35 are used), followed by the action to take. Reaching it queues it; once
    a page ends, the queue is tested every frame.
  - Kind 1 is a command (table at `0x550C80`).
  - Kind 2 is a branch to a page: codes 1, 4, 5, 0x16, 0x21, 0x22, 0x2B and
    0x2D are used (5 and 0x22 also drop the condition that led there). What
    each does wasn't traced, since nothing runs them.

## Behaviour

The console is COMBAT.RAS with the 3D view in its hole at (52,10), 532 x 231,
60 degrees across. The game reads the module's result as 0 the Enterprise
destroyed, 1 lost (warped out), 2 or 3 won (the fleet is marked beaten).

- At the start (`0x47B688` → `0x49461C`) the ship nearest the Enterprise is
  the target, and the Enterprise is turned to face it, level (`0x4A6639`,
  `0x4A2A80`); the player line's yaw and pitch go unused. With yaw read from +y
  towards -x, 66 of the 68 enemies the missions start with face the
  Enterprise (read the other way, 26).
- Cloaking: a ship's +0xD4 runs from 0 (seen) to 0x4000 (cloaked), stepping
  0x444 a frame by its rate +0xD8 (`0x4AD474`); fully cloaked, it sits out
  of the fight (no AI) and drops off the radar (`0x4AAC68`), and once its
  timer (+0xEA0, 20 s) is up it starts to uncloak (`0x4AD604`). Starting to
  cloak (`0x4AD3B4`, cue 0x3E) is never called: no ship cloaks during a
  fight. Ships of later waves are made fully cloaked and hidden at load and
  uncloak (cue 0x3F) when their wave comes in (`0x4A2FA9`); destroyed ships
  go to 0x4000 to vanish (`0x4AC2D3`). Both cloaking and uncloaking act only
  with flag +0x2C 0x2000, which nothing in the exe is seen setting. The
  engine's hidden waves and destroyed ships behave the same, without the
  fade.
- The forward view (`0x4A0D13`) sits two of the Enterprise's radii out along
  its own axis, ahead of it, so the Enterprise is never in sight. The enemy
  view (`0x4A1E85`) sits one radius out towards the target and looks at it,
  keeping the Enterprise's up. Both add the viewscreen zoom × 600 to that
  distance.
- The viewscreen zoom slider (618,127-633,180, hotspot 14): clicking or
  dragging in it sets the knob to that row, 18 steps from top to bottom
  (`0x495B49`), and the zoom is the steps below it, 0 to 18 (`0x4AEDFE`, 0 at
  the start). SVIEWN.RAS is drawn above the knob and the lit SVIEWF.RAS below
  it. Besides moving the camera out, the zoom spreads the stars from the
  centre by 1 + zoom/30 (`0x4ADC6A`).
- The systems panels (`0x49E232`) show the system display: S???_STR.RAS
  numbers each pixel with a system (1-4 the shields, 5-12 weapons to cloak),
  and each system's pixels come from S???_0 (working, bright), _1 (down or
  damaged, dim) or _2 (destroyed, red); unnumbered pixels from _1
  (`0x49EAF6`). The buttons at their feet (left 174,435 for the target,
  right 449,435 for the Enterprise; BSCANL/BSCANR N and F) switch a panel to
  the shield facing display and back, with "Shield facing display enabled" /
  "System display enabled" (COMP0059/0058, `0x4AEC22`): below the bars, 57
  rows down to row 461, black, with the ship drawn from three times its
  largest extent (+0x30) out towards the other ship, 60 degrees across
  (`0x49F88F`). Round the ship is its shield sphere (`0x49FC1E`):
  `SHLD8k_c.3DV` (k 4 for a ship with four shields, else 0; c the ship's
  place in the mission, 1-9 then a-f), a 26-point sphere whose four
  quarters take the four materials of `SHLD8n_0.MTL` (n its number of
  shields): with four, magenta, blue, green and red; fewer repeat them. It
  is scaled to 0.8 of the largest extent per 100 units (`0x4A3BB4`).
- Shields hit (`0x49A3D1` torpedoes, `0x49B6C5` phasers), before the
  damage: if the facing isn't destroyed and the ship's shield isn't already
  flashing, it flashes (`0x497B99`); otherwise the Enterprise only jolts.
  The flash is `SHIELD_c.3DV`, a shallow dome 208 across, with the ship's
  `?_SHIELD.SPR` animation put in place of its `SHIELD.LBM` texture. Each
  frame (`0x497D2E`) it is turned to face where the hit came from (the
  torpedo, or the firing ship) and set twice the ship's largest extent out
  from its middle that way, scaled by the mean of the ship's extents over
  412.667 (the Enterprise's, so the dome fits it as made; `0x4A3A72`). It
  shows until the animation ends. The Enterprise's ends at once, unseen.
  The sound is `SHIELDS1`-`3.WAV` for the Enterprise, `KSHIELD1.WAV` for the
  rest. A torpedo leaves no explosion of its own.
- The Enterprise's jolt (`0x497B00`): the screen shakes for 0x91 ticks (the
  clock runs at 291 a second), every other frame moved right by 0..5 and
  down by 0..3 pixels (`0x47BEFF`). If the facing hit is more than four
  tenths gone (t tenths), the screen also dims for (4t - 16) × 9 ticks: a
  level runs from -127 to 127, and while it's within ±63 every colour is
  quartered, otherwise halved (`0x47BC14`, `0x47C2EC`).
- Held on the viewscreen (hotspot 17, `0x4AEFD4`) the mouse steers:
  (middle − mouse) × 100 / half the hotspot for each axis, from (319,124),
  joins the stick's axes until let go. `0x4A6F7D` takes the yaw rate down by
  x and the pitch rate up by y, so left of the middle turns left (as Turn
  Left does) and above it climbs (as Climb does, the Down key).
- The starfield (`0x4ADC6A`, shapes `0x4AE2BF`): stars of 13 types; type 1 a
  cross, 3 one 0xA9 pixel, 4 an X, 5 a plus, 6 and 7 three-pixel bars, the rest
  one pixel, 0x84 or (one time in eight) 0x87, in STANDARD.PAL. Seven more
  stream out from the middle: each frame their offset from it grows by
  1 + speed / top speed × 0.075, and out of the view (or back within 30
  pixels of the middle going astern) they start again anywhere within 952
  pixels of it at scale 1.
- On the radar a ship blowing up is SEXP1/SEXP2 by turns (`0x4AB6DA`).
- The torpedo count is font 1: 0-9BIG.RAS's 6 x 20 digits, 8 apart, at
  (369,408) (`0x4AA6BB`, `0x49E01B`).
- The radar's slider (450,317-465,371, hotspot 13) works the same way, with
  SRADARN/SRADARF. Its step s sets the range to (19 - s) × 7500 + 10
  (`0x4AB7E2`); the fight starts at step 12, 52510 (`0x49D640`).
- The radar (`0x4AB102`, drawn by `0x4AB5C1`) is round the Enterprise, which
  faces up. A ship more than the range off on any axis isn't shown. The rest
  sit on the plane at (321 + sideways × 102 / range, 320 − ahead × 24 /
  range), on a stalk as tall as height × 24 / range. The stalk is white with
  the 11 x 11 marker (S???MAP, MAPH for the target) at its top for a ship
  level or above, and red with the marker at its foot for one below. Ships
  more than 96 pixels to the side or 18 ahead or behind, outside the plane's
  ellipse (half-widths at 0x54F750) or off rows 280-360 aren't drawn.
- The right panel shows the Enterprise's schematic (SENT_n). Clicking the CONN
  line (262,440-438,460, `0x4AECC6`) calls up the helm menu (CONNMNU0 at
  (472,310)) over it, and letting go puts it away. The CONN line shows the
  order in hand, row `mode`×19 (176 x 17) of CONNBUTT.RAS at (262,442).
- The readouts are in FONTLIT (A-Z_0-9.RAS, whose cells include their
  spacing). The ships' names go at (2,310) for the target and (604,310) for
  the Enterprise: the class's name from `0x54E660` (NCC-1701D, BIRD OF PREY,
  WARBIRD, DREADNOUGHT, VORCHA, PROBE), with " n" for the second of a kind and
  on. Their speeds are `SPEED: %.3f` of the class's top speed at (123,449) and
  (470,449) (`0x49F302`), and the range is `RANGE: %6.0f` at (209,365). FONTLIT's
  minus sign looks like "=".

### The enemies' AI

Each enemy runs `0x4A7C91` every frame, then the steering at `0x4A53A1`. The
AI's clock (`0x47B83E`) counts 291.3 ticks a second.

- **Firing.** Every 2037 ticks (seven seconds): phasers if the Enterprise is
  within the class's phaser range (`0x49ABBC`: each charged bank whose firing
  octant covers it, each shot a roll to hit), and the photon tube facing it,
  if it's loaded and the Enterprise is within photon range (`0x4999AB`: all
  the tube holds, spread 230 angle units apart).
- **Closing.** Beyond three quarters of phaser range, the waypoint is the
  Enterprise, at full speed. Inside it, no new waypoint is set and the ship
  holds its last turn, so it sweeps past and circles out until it's far
  enough to dive in again.
- **Collisions.** Within 1000 units, if the two ships are moving (or closer
  than their radii plus 500) and are, or will be a second later, closer than
  the larger radius plus eight times the faster speed (`0x4A7880`), the ship
  swerves for 1455 ticks to a point 1100 units to either side of and below
  the Enterprise.
- **Running off** (`0x4AFF2A`): to a point two of the Enterprise's phaser
  ranges off it on each axis, side and height at random, below it; at most
  every 2910 ticks. A ship does this when a hit leaves two or more of its
  systems destroyed (`0x4B0103`), and Soran (`soran`, `soran9`, `quarash`:
  flag 0x40000) does it all the time while any other ship is left in the
  fight, including waves still to come.

## Sources and checks

From the notes read off `sgens.exe` and the game's files, the 2 CD Windows release, while the engine was STG-Engine: [combat](../research/combat.md), in [research/](../research/README.md). [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) says how they were made. All values are little-endian.
