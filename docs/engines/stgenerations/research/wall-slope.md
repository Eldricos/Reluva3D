# LV2 walls: is +0x20 a slope? (No: it is the wall's heading angle)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

## Verdict

+0x20 is **not** a normal z component. It is the wall's **direction angle in
turns** (16.16 fixed, 0x10000 = 360 degrees), i.e. the angle of the edge
v1 -> v2:

    a = atan2(y2 - y1, x2 - x1) / 2pi        (mod 1)
    n = (nx, ny) = (sin a*2pi, -cos a*2pi) = (dy, -dx) / L   (normal on the right of v1->v2)

Walls are always vertical quads. There is no lean, no per-end height and no
second vertex pair.

## Code evidence

- `0x4B76F0` (recompute angle, ecx = wall): `dx = V[+0x88].x - V[+0x86].x`,
  `dy = V[+0x88].y - V[+0x86].y`, `call 0x4BC560` (table atan2: 0x2000 = 45 deg,
  quadrant offsets 0x8000/0xC000, table at [0x552160]), then `mov [esi+0x20], eax`.
- `0x4B7670` (recompute line from angle): `idx = ((u16)wall[+0x20] >> 4) & 0xFFF8`
  into the 8-byte (sin, cos) table at [0x55215c] (512 steps per turn);
  `nx = T[0]` -> +0x1C, `ny = -T[1]` -> +0x24, `d = -(nx*x2 + ny*y2)` (vertex +0x88) -> +0x28.
- Both are called only from `0x4B9119` / `0x4B9122`, in the scripted-object
  move/rotate code, right before `0x4B8230` snapshots the wall (the snapshot copies
  +0x20 to snapshot+0x24 but the distance routine `0x4B91C0` reads only nx, ny, d,
  bottom and top). So +0x20 is the persistent wall heading that rotations change;
  nx/ny/d get rebuilt from it.
- Nothing in the draw path (`0x4C7D00`, `0x4C8420`, `0x4C8F80`) or collision
  (`0x4BA670`, its callers `0x4B9F56`, `0x4BAE19`, end-cap test `0x4BA1E0`) reads wall +0x20.
  (A sweep of every `[reg+0x20]` read in 0x4B0000..0x4D0000 found no other wall consumer;
  the `0x4BA9E0` hit is actors (x,z,y at +0x1C/+0x20/+0x24), the `0x4C7BB0` hit is billboards.)

## Drawing (exact)

Vertex transform `0x4C3610` / `0x4C36C0` (per 2-D vertex, 16 bytes at [0x57a588]):

    rx, ry = (x - eye.x, y - eye.y)
    cam.x  (+0) = 2*(rx*s - ry*c)      (c, s = [0x57d9cc], [0x57d9d0], <<15)
    depth' = 2*(rx*c + ry*s)
    cam.d  (+0xC) = depth' + 1.0       (the "eye 1 unit back")
    cam.h  (+4) = depth' * shear - eye_z   (shear [0x57d9d8] = look up/down, eye_z [0x57d9bc])

Only one camera-space value per vertex; heights are added afterwards:
`0x4C80CD`: draw rec +4 = wall +0x2C (top), rec +8 = wall +0x30 (bottom);
rec +0x5C / +0x6C = transformed v1 (+0x86) / v2 (+0x88). In `0x4C8F80`:

    sx_i      = proj(cam.x_i / cam.d_i)                       (0x4C8FC3, 0x4C8FFF)
    sy_top_i  = proj((cam.h_i + top)    / cam.d_i)            (0x4C9174..0x4C919D)
    sy_bot_i  = proj((cam.h_i + bottom) / cam.d_i)            (0x4C9246..0x4C926E)

So the four world corners are exactly

    (x1, y1, top) (x2, y2, top) (x2, y2, bottom) (x1, y1, bottom)

with top = +0x2C, bottom = +0x30 for both ends. No horizontal offset.

Culling (`0x4C7D97`): skipped if `nx*ex + ny*ey + d < 0` with the eye moved back 1 unit,
unless flags 0xC0.

## Texture mapping

- u (`0x4C9393`): `u0 = +0x44 + (i16 +0x60 * time & 0x1FFFF) << 6` (u scroll is +0x60;
  v scroll +0x62). With +0x70 = +0x74 = 0: one end gets u0, the other u0 + +0x48.
  Otherwise u_v1 = u0 + +0x70, u_v2 = u0 + +0x74 (running u along a strip; in verid1 5 walls,
  tex 35, flag 0x1, with +0x74 - +0x70 = +0x48). Which end gets which is chosen by side
  (sign of n·eye + d) and flags: front side swaps if flag 0x20, back side swaps if
  (bit 0x40000 xor bit 0x20). u is interpolated perspective-correctly along the wall
  (clip params rec +0x24 / +0x38).
- v: rec +0x34 = +0x4C (+ +0x62 scroll), rec +0x48 = +0x50. v runs from +0x4C at the top edge
  to +0x4C + +0x50 at the bottom edge, linearly in height (vertical wall, so slant = z).
  verid1: +0x50 / (top - bottom) is a whole texel rate (12.8, 8.53, 6.4, 32, 25.6, ...).

## Collision

`0x4BA670`: `s = nx*p.x + ny*p.y + d` (p = [ecx] x and [ecx+8] y), radius test, back side only
with flag 0x8000; hit direction returned as (ny<<12, -nx<<12). No height term, no +0x20.
Height use is only the overlap test at `0x4B9F0E..0x4B9F26` (+0x30 bottom <= body top,
+0x2C top >= feet + step), scalar for the whole wall.

## Per-end heights

None. +0x2C / +0x30 are the only heights, used for both ends in drawing and collision.
Fields 0x34, 0x38, 0x54, 0x58, 0x8C, 0x90 are zero in verid1.

## verid1 statistics (2815 walls, 3311 vertices)

| off | distinct | range / note |
|-----|----------|--------------|
| 0x00-0x13 | const 0 | runtime links / texture ptr (+0x10) |
| 0x14 | 13 | event id |
| 0x1C nx | 732 | -1..1 |
| 0x20 angle | 793 | -0.135..0.8705 turns |
| 0x24 ny | 751 | -1..1 |
| 0x28 d | 2130 | -211.7..296.7 |
| 0x2C top | 155 | -80..100 |
| 0x30 bottom | 139 | -130..50 |
| 0x3C,0x40 zones | 29 / 12 | |
| 0x44 u0 | 201 | -10..108.9 |
| 0x48 u len | 1012 | 0..1434 |
| 0x4C v0 | 165 | -85.3..126 |
| 0x50 v len | 273 | -12.8..2901 |
| 0x5C | const 0x3A980000 | |
| 0x60 | const 0 | u/v scroll speeds |
| 0x64 | 9 | light |
| 0x68 | const 0x100 | |
| 0x6C flags | 132 | 0x100,0x200,0x10000 always; 0x40 1939, 0x20000 1405, 0x4000 1362, 0x2 859, 0x80 658, 0x20 506, 0x8000 337, 0x40000 292, 0x4 199, 0x1000 123, 0x1 5, 0x2000 1 |
| 0x70, 0x74 | 6 | per-end u (5 walls) |
| 0x78 | 5 | 0..3 extra thickness |
| 0x80..0x85 | | anim speed / count / frame |
| 0x86, 0x88 | | vertex indices |
| 0x94..0x97 | 97 | texture id at 0x96 |

Checks:
- nx² + ny² = 1.0000 for every wall (except one zero-length wall, #1876, n = 0, angle 0),
  so there is no room for a third unit-normal component.
- Every wall: |nx*x + ny*y + d| < 0.005 at both vertices, and n = (dy, -dx)/L for all 2814
  non-degenerate walls (v1->v2 has the normal on its right).
- angle vs atan2(dy, dx)/2pi (mod 1): max error 0.003 turns (table atan), mean 0.0003.
  Examples: n=(0,1) -> 0.5, (1,0) -> 0.25, (-1,0) -> 0.75, (0,-1) -> 0, (-0.707,-0.707) -> -0.125.
  The stored value wraps irregularly near -0.15 (some walls hold 0.85 instead of -0.15);
  compare modulo 1.

Likely cause of odd-looking cave walls: not lean. Look at u direction flips (flags 0x20 /
0x40000 by side), +0x70/+0x74 strip u, two-sided flags 0xC0, or negative v length.

## Addendum: halee1 and wall flags

halee1 (7895 walls): +0x20 matches atan2(dy, dx)/2pi (mod 1) for every non-degenerate wall,
max error 0.0006 turns. So it is the heading here too. The "complementary pairs" (a, 1-a) are
segments mirrored in y (dy negated); they don't pair walls. 415 walls have a reversed twin
(same vertices, opposite order), and 1237 walls share a vertex pair with another wall
(stacked walls on one segment at different heights). Draw each one separately.
Walls near (-458, 220): #255 (-450,210)->(-445,175) and #287 (-450,210)->(-455,210),
top 60, bottom 35.1. Their n, d and angle are consistent.

Wall flags (+0x6C), code addresses:
| flag | effect | where |
|------|--------|-------|
| 0x2, 0x4, 0x8 | column-table methods, masked (see billboard-draw.md) | 0x4C80A6, 0x4C80E8, 0x4C0DFD |
| 0x20 | front side: swap u ends (mirror) | 0x4C93CB / 0x4C93F7 |
| 0x40000 | back side: u swapped if (0x40000 xor 0x20) | 0x4C93D3 / 0x4C940E |
| 0x40 / 0x80 | occluder bits, rec+0x58 bit1/bit2, per-column window cut below / above | 0x4C80B1, 0x4C0E5D |
| 0x100 | visible / linked | 0x4C7D4C, collision |
| 0x200 | pickable: hit test at screen point (texel != 0xFF) when byte [0x552168] bit 0 is set, unless 0x400 | 0x4BC784 (from 0x4C12D3) |
| 0x4000 | seen from behind: rec+0xC = 2, the wall's occluder bits still apply but it is NOT painted (invisible occluder) | 0x4C936F, 0x4C0E95 |
| 0x8000 | seen from behind: drawn normally; also blocks from both sides in collision | 0x4C936A, 0x4BA6E8 |
| 0xC000 (either) | skips the early back-face cull | 0x4C7D92 (`test dh,0xC0`; formats.md "0xC0 both sides" really means 0xC000) |
| 0x10000 | no draw or collision reader found. Saved and restored with 0x40/0x80 by the scripted-object move (snapshot 0x4B8230 -> +0x40 bit2, restore 0x4B93ED) | |
| 0x20000 | set by a script native for all walls of an object (0x4B5F70, set when arg==0), and checked at 0x4B6260 (collects walls with 0x800 and without 0x20000; no direct callers). No draw or collision effect found | |
