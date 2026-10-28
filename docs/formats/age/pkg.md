# .pkg models and .mtx placements

Midtown Madness 2's models, `geometry/*.pkg` in `mm2core.ar` (1,040 of
them: cars, buildings, props, bridges), with `.mtx` files placing their
parts. Read by `src/engines/age/pkg.cpp`; `reluva-tool pkg-info` shows
one or reads a game's.

## How this was worked out

From the retail files alone. A hex dump of `vpbug.pkg` (the Beetle)
showed `PKG3`, then `FILE` and a length-prefixed name (`BODY_H`) with a
size, then counts and runs of floats in eights whose 4th to 6th made unit
vectors (normals). The value 0x112 after the counts is Direct3D's
flexible vertex format code for exactly that layout (position, normal,
one pair of texture coordinates; Microsoft's DirectX documentation), and
the vertices were followed by a count and 16-bit indices below the
vertex count. With that, a parser was run over every model and checked
that each section ends exactly where its size says and that the vertex
and index counts add up to the totals in its header: 4,666 of the 4,668
meshes do (the two that don't are below).

The shaders' layout was found the same way, checked by every one of the
1,034 shader sections ending exactly at its size. Which colour is which:
glows (`fxltglow`, the traffic lights' `s_red_glow`...) have no diffuse
colour and keep theirs in the 4th of the float colours, which in
Direct3D's material order (diffuse, ambient, specular, emissive) is
emissive, and in the 3rd of the byte colours. The byte colours' order
comes from the traffic lights: read as R, G, B the red, amber and green
glows are red, amber and green; read as B, G, R (Direct3D's own order)
they're blue, teal and green.

The winding: of 31,330 triangles sampled, 31,168 are counter-clockwise
seen from the side their vertices' normals point to. The coordinates are
right-handed with Y up and cars facing -Z: the Beetle's headlights are
at -Z, its left front wheel at -X, and its steering wheel on the left
(a left-hand-drive car); text on the bus reads the right way round.

## Layout

All numbers are little-endian; u32 unless said.

The file is `PKG3`, then sections, each:

| Size | Contents |
|---|---|
| 4 | `FILE` |
| u8 + text | name: a length byte counting a terminating zero, then the name and the zero |
| u32 | the section's size |
| | its contents |

Sections named `shaders`, `offset` and `xrefs` are described below; any
other is a mesh.

### Meshes

Named `<part>_<level>` (`BODY_H`, `WHL0_M`, `HEADLIGHT0_H`), or just
`<level>` for a model in one piece (buildings, props). Levels of detail
are `H`, `M`, `L` and `VL`, finest first.

| Field | Contents |
|---|---|
| groups | number of groups |
| vertices | total vertices, all groups |
| indices | total indices, all groups |
| groups again | the same number |
| fvf | Direct3D's flexible vertex format: 0x112 (position, normal, texture coordinates; 4,598 meshes) or 0x102 (no normal; 68) |

Then each group:

| Field | Contents |
|---|---|
| strips | how many lists follow (1 in every retail mesh's first group, the only ones checked) |
| shader | the shader, within a paint job |

and each list:

| Field | Contents |
|---|---|
| type | always 3: a triangle list |
| count | vertices |
| | the vertices: 3 floats position, 3 normal (if the format has them), 2 texture coordinates |
| count | indices, a multiple of 3 |
| | u16 indices into this list's vertices |

### Parts

| Part | What |
|---|---|
| BODY | a car's body |
| SHADOW | a flat quad under a car, drawn with a dark texture |
| WHL0-WHL3 (TWHL for trailers) | wheels: in the Beetle 0 front left, 1 front right, 2 and 3 the back (from the .mtx positions) |
| HEADLIGHT0, HEADLIGHT1 | a square where each headlight is, untextured (its effect isn't known yet) |
| HLIGHT, TLIGHT, BLIGHT, RLIGHT, SLIGHT0-1 | lit headlights, tail, brake, reverse and indicator lamps (only ever at level L) |
| SIREN0-1, SRN0-3 | sirens (level L) and their glows (level H) |
| BREAK01... | pieces of props that come off when hit |
| REDGLOWDAY, ...NIGHT, WALK_DAY, NOWALK_NIGHT... | traffic lights' and crossings' lit states |
| dash, roof, wheel, speed_needle... | the dashboard models (`*_dash.pkg`) |

### Placements (.mtx)

A part with a file `geometry/<model>_<part>.mtx` (`vpbug_WHL0.mtx`) is
placed by it: 12 floats, 48 bytes.

| Floats | Contents |
|---|---|
| 0-2 | the part's bounds, minimum |
| 3-5 | maximum |
| 6-8 | a pivot: what needles and the steering wheel turn about |
| 9-11 | where the part goes |

Wheels, headlights and some breakable pieces are modelled about their own
centre and moved to the last three floats; dashboard parts are modelled
in place with a pivot and no move. Parts without an `.mtx` (BODY, SHADOW,
the lamps, most breakable pieces) are in place.

### Shaders

| Field | Contents |
|---|---|
| kind | low 7 bits: paint jobs (a car's colours; 1 to 10, and 16, 17, 24 and 48 in a few models); 0x80: colours are bytes, else floats |
| per job | shaders in each paint job |

Then paint jobs x shaders each, each:

| Field | Contents |
|---|---|
| texture | a name as above, or a length of 0 for none; looked up as `texture/<name>.tex`, else `texture/<name>-0001.tex` (animated, see [tex.md](tex.md)), else `texture/<name>.tga` |
| colours | bytes: diffuse, ambient, emissive, each R, G, B, A; floats: diffuse, ambient, specular, emissive, each R, G, B, A |
| shininess | float |

A group's shader is `shaders[paint job * per job + group's shader]`. Of
10,321 texture names, 16 aren't in the game's archives (some London
brick, grass and mud, and damage textures for one car's purple and
yellow paint).

### offset

Three floats; 863 models have one. Not known yet.

### Where models stand

Many models are modelled centred on their own origin, half below it
(115 `sp_` props, the subway pieces, landmarks such as
`ol_natgallery_l`); building fronts (`sw_`, `cw_`, `nw_`...) stand on it.
Cities' `.inst` files place either kind where it belongs. A traffic
light's glow parts (`REDGLOWDAY_H`...) don't fit its centred body: they
sit about 4.6 m above its signal heads, as if the body stood on its
origin. How the game places them is still to be found.

### xrefs

Models placed inside this one (the Hyatt's red warning lights, bridge
lamps, the cannery's awnings): a count, then each 48 bytes of matrix
(three axes and a position, 12 floats) and a 32-byte name field (the
name, a zero, and whatever was in the buffer before). The viewer lists
them; drawing them is left for the city.

## What doesn't load

Six of the 1,040 files, none used by the game:

- `sky_dawn.pkg`, `vpdb731.pkg`: an older version, `PKG2`.
- `thing.pkg`: a test file whose mesh `M` has a size of 0 with its data after it.
- `vpvw_dune.pkg`: two wheel meshes overwritten with text (the shipped
  dune buggy is `vpdune.pkg`).
- `wf_hse_baywin03_stone_wht_5s_12_l.pkg`, `ww_bldg1_stone_tan_t6_20_l.pkg`:
  empty.
