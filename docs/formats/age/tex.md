# .tex textures

Midtown Madness 2's textures, `texture/*.tex` in `mm2tex.ar` (3,665 of
them). Read by `src/engines/age/texture.cpp`; `reluva-tool tex-info`
shows one or surveys a game's.

## How this was worked out

From the retail files alone. Grouping all 3,665 by their first fields
showed a width and height, a small set of format numbers (1, 14, 17, 18)
and a count from 1 to 10 that grew with the file size for the same image
size. A file's size then came out exactly as 14 bytes of header, 1,024
bytes of palette for formats 1 and 14, and each mipmap level (each half
the last, down to 1 pixel) at 1, 3 or 4 bytes a pixel: true of all 3,665.
The colour order was settled by looking, and differs between the
formats. The palettes are B, G, R, A: drawn that way the fire truck's
body (format 1) is red and San Francisco's map has blue water, the other
way round they're blue and orange. The 24- and 32-bit formats are R, G,
B(, A): the US roads' centre lines (`r2_f`, format 17) come out yellow
that way and cyan the other, and the red traffic light's glow
(`s_red_glow`, format 18) averages 194, 2, 0 in its first three bytes.
(The first version of this page had them all B, G, R, from the palette
textures alone; the city's roads showed it wrong.)

## Layout

| Offset | Size | Contents |
|---|---|---|
| 0 | u16 | width |
| 2 | u16 | height |
| 4 | u16 | format |
| 6 | u16 | mipmap levels stored |
| 8 | u16 | always 1 |
| 10 | u32 | flags |
| 14 | | palette (formats 1 and 14): 256 colours of B, G, R, A |
| | | the levels, largest first, rows top to bottom |

| Format | Pixels | Count |
|---|---|---|
| 1 | 8-bit palette indices; every palette's alpha is 255 | 2,931 |
| 14 | 8-bit palette indices, the palette's alpha used (signs, wheels, trees) | 139 |
| 17 | 24-bit R, G, B (roads, ground, skies, some facades) | 300 |
| 18 | 32-bit R, G, B, A (glows, dashboards, decals) | 295 |

Sizes run from 8 to 650 pixels a side, and 148 aren't square. All but
two are powers of two: `nl01_coit_top` (64 x 73) and
`sf_wall_brick03_blkfence_2_l` (650 x 597), whose levels still halve
(rounding down).

## Flags

| Flags | Count | Seen on |
|---|---|---|
| 0x2 | 1,533 | building fronts (`_f`, `_l`), windows |
| 0x10001 | 940 | cars' bodies and wheels |
| 0x0 | 639 | props, signs, menus |
| 0x18001 | 278 | cars' backs, damage textures, dashboards |
| 0x8006, 0x18006 | 153 | roads and ground |
| 0x8000 | 44 | glows (`fxpt*`), some car textures |
| 0x10000 | 35 | skies |
| 0x6, 0x7, 0x10002, 0x10003, 0x10006 | 43 | decals (crossings), a few fronts |

Bit 0 keeps a texture to its edges: of the 345 textures with it that the
game's models use, 332 have their texture coordinates within 0-1 (to a
fifth over), and the rest overshoot by a little or have broken
coordinates; textures without it are repeated freely (pediment trims 17
times across, rails 60). Reluva clamps those with bit 0. Bit 1 isn't
"don't repeat": 273 of the 282 building fronts with it are repeated
across the street and 252 up. 0x4 marks the ground's surfaces: every texture with it is a road or
ground texture (roads, low-detail roads, grass, gravel, stone) or a
ground decal or concrete. Of the city textures drawn only on the
ground, 48 of 55 have it (not the sidewalks, the water or `nf_patio`);
of those drawn only on walls, 1 of 417. Of the 131 textures with a
physics material other than `none`, 117 have it, the 14 without being
the sidewalks and `s_water`. What the game does with it isn't known.

0x8000 is on what the player sees closest and largest:
- the cars' backs, which the chase camera looks at: 214 of 247 (`_bk`,
  `_tpbk`, `_bklft`, `_bk1` and their `_dmg`), but only 9 of the 95
  lowest-detail (`_vl`) backs, so it goes with being seen near, not with
  being a back;
- the cockpit: 79 of 93 dashboards, steering wheels and needles;
- every 24-bit ground texture (153 of 153 with 0x4 in format 17; none of
  the 16 ground textures in palette formats, none of the 147 other
  format-17 textures);
- the particles (`fxpt*`).

The car's other textures almost never have it (3 of 886). 111 of the
475 have a single mip level, mostly the 256-pixel dashboards, which never
shrink on screen. A few inconsistencies (one car's back without it while
its `_dmg` and `_vl` have it) say it was set by hand per file. It isn't
the shaders, the vertex format, the level of detail or how the mips were
made. Reluva reads it as "keep full detail": the help's Texture Quality
(Very High, High, Medium, Low, for "surface textures") leaves these
alone. Better filtering for near surfaces would fit too; footage at a
low texture quality would tell them apart.

0x10000 isn't known. It's on 1,055 of the 1,166 car textures (not the
fire engine's and semi's shared `vplafrance*`, the F350's backs, the
trailers, glows or `carbottom`), all 32 skies (in San Francisco every
24-bit single-level `sky_*_f`, none of the palette `sky_*_l`), the
plain lane roads (33 of 33, 21 of 26 `_fa`, 3 of 44 `_lo`, 1 of 45
junctions, crossings and ground) and about 40 building and prop
textures (`sw_stc_*` garages and doors, `tl_challfront*`, `kl_` museum
textures, `p_fence_blk*`, `treesmall*_s`). The full-detail member of each
sky and road pair has it, which suggests a texture-quality tier; it
isn't the shaders, reflections (their maps are separate, and trees and
fences have it), the size, format or mips. No file lists texture
properties.

## Animated textures

Water is animated: `texture/s_ocean-0001.tex` to `-0030` and the like
(121 frames in all for `s_ocean`, `s_pond`, `s_thames`, `s_water`), with
`tune/s_ocean.movie` and the others giving the rate (`rate 20`; 15 for
ponds, 30 for the Thames and `s_water`), frames a second. Models name the
texture without the frame number (`s_ocean`), the cities' PSDL with one
(`s_ocean-0007`, `s_thames-0009`). Reluva loads every frame of a texture
that has a `.movie` and shows them in turn at that rate, from the one
named.
