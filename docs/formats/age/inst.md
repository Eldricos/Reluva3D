# .inst placed models

The models a city places (buildings, lamp posts, bridges, signs):
`city/sf.inst` (3,763) and `city/london.inst` (1,997) in `mm2core.ar`.
Read by `src/engines/age/instances.cpp`.

## How this was worked out

From the two files. The first record was a 16-bit value, another, a
byte, a model's name and floats; the byte was the name's length (with
its terminating zero) plus 0x80. Records whose byte had no 0x80 were
followed by 12 floats, whose first nine made three axes of length about
1 and whose last three a position in the city; those with 0x80 by 5
floats, two then a position. With that, both files read to their exact
end, and every name is a model in `geometry/`. The 16-bit value is a
PSDL block number (it never exceeds the city's blocks, and neighbouring
records share it).

## Layout

Records one after another to the end of the file:

| Field | Contents |
|---|---|
| u16 | the PSDL block the model stands in |
| u16 | flags, not worked out (0 and 0x100 are commonest) |
| u8 | the name's length, counting its zero; 0x80 added for a model turned only about the vertical |
| | the name: `geometry/<name>.pkg` |
| 12 floats | three axes (X, Y, Z), then the position; or, with 0x80: |
| 5 floats | the X axis's x and z, then the position |

The axes may be longer than 1 (106 London models are stretched by 1.33
along Y; many San Francisco ones by 0.97-0.99 along X): they're scaled.

For the short form Reluva takes the X axis as (x, 0, z) and the Z axis
as (-z, 0, x), the turn a right-handed system gives. Checked against the
blocks: turned this way, 284 of the short-form models that aren't square
to the axes line up (to within a right angle) with an edge of the block
they stand in better than turned the other way, which 5 do (126 are
ties).

| | San Francisco | London |
|---|---|---|
| records | 3,763 | 1,997 |
| short form | 211 | 544 |
