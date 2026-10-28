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

## Flags (not known yet)

The flags' meanings haven't been worked out; Reluva reads them and
doesn't use them yet. What they go with:

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

A guess to test when the city is drawn: bits 0 and 1 may stop the
texture repeating across or down (a building front repeats along the
street but not up it; a car's body texture doesn't repeat at all), and
0x8000/0x10000 may be about mipmaps or lighting.

## Animated textures

Water is animated: `texture/s_ocean-0001.tex` to `-0030` and the like,
with `tune/s_ocean.movie` giving the rate (`rate 15`, frames a second
presumably). Models name the texture without the frame number
(`s_ocean`). The viewer shows the first frame.
