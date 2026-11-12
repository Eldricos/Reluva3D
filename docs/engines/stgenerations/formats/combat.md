# Space combat (COMBAT.DAT)

> **Code:** `src/engines/stgenerations/formats/combatdata.h`, `src/engines/stgenerations/game/combat.h` · **Confidence:** layout confirmed; behaviour from the code unless marked

## Overview

Space combat's files, in `COMBAT.DAT`.

## Layout

| File | Format |
|------|--------|
| `.3DV`, `.3DR`, `.PCn` | DRE object v1.10: parts of triangles with a material, 16.16 UVs, face normals; x across, y up, z forward |
| `.MTL`, `.MTR`, `.DMG` | 36-byte materials: RGB555 colour, flags (1 textured, 2 effect, 3 flat), texture name |
| `.IMG` | GIF87a textures, each with its own palette |
| `.LBM` | IFF PBM effect textures |
| `.RAS` | Sun rasterfiles: the console; colour (255,0,163) is see-through |
| `.SPR` | animations ('SPRT', 'TIME', 'COMP' frames packed four ways) in `STANDARD.PAL` (6-bit) |
| `.MET` | big-endian glyph cells into `SCANNER.RAS` / `A-Z_0-9.RAS` |
| `SYSTEMS.DAT` | ship classes: speed, agility, 27 systems (shields 1-4, weapons, life support, engines, power, computer, sensors, damage control, cloak, 10 phaser banks with firing octants, 4 photon tubes), the shields' leak tables, phaser and photon range and damage |
| `SCn.MIS` | `SpaceCombat n`: `:wave` and ship lines `id type x y z yaw pitch velocity AI ...` (x, y the plane, z altitude; AI `soran`, `quarash` or `standard-N` to aim at system N) |
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

- The forward view (`0x4A0D13`) sits two of the Enterprise's radii out along
  its own axis, ahead of it, so the Enterprise is never in sight.
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

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
