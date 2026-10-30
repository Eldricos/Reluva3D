# Visibility (`.cpvs`, `.pvs`, `.pvshist`)

Which of a city's PSDL blocks ([psdl.md](psdl.md)) can be seen from
which: `city/sf.cpvs` and `city/london.cpvs`, with the files they were
made from beside them. Read by `src/engines/age/pvs.cpp`; the scene draws
only the blocks seen from the eye's block, and the models standing in
them.

## How this was worked out

From the files. `sf.pvs` and `london.pvs` are 4 MB each, all 0 but for
bytes of 3; read as 2,048 rows of 2,048 bytes, row *b* column *c* is 3
where block *c* can be seen from block *b* (the last row used is the
city's last block). `sf.pvshist` is text, lines of "block, block, 0-255":
every pair the `.pvs` has, with a score. The `.cpvs` starts `PVS0` and a
count one more than the PSDL's stored block count, then words that grow
like offsets; the bytes after them looked like 2-bit patterns (`0x03`,
`0x0f`, `0x3c`, `0xcc`) between small counts. Read as the runs below,
every row of both cities unpacks to two bits for each block, low bits
first, 3 or 0, and agrees with the `.pvs` but for 9,339 (San Francisco)
and 11,238 (London) pairs it drops, and one it adds in San Francisco. The
variants `sf_0.cpvs`-`sf_255.cpvs` get smaller as the number grows:
`<city>_N.cpvs` holds exactly the `.pvshist` pairs scoring more than N
(checked for every N, 0 to 255, in both cities; scores run 2-255).
`sf.cpvs`, `london.cpvs` and `london_bad.cpvs` are byte for byte the
`_8` files, so the game's own cut is 8, and the pairs it drops from the
`.pvs` are exactly those scoring 8 or less (`sf_8` keeps 95,683 and
drops 9,339). `sf_00`, `sf_254` and `london_254` have 1,165 and 1,342
rows, from an older build of the cities.

## `.cpvs` layout

Little-endian.

| Field | Contents |
|---|---|
| `PVS0` | |
| u32 | *n*: the PSDL's block count as it stores it (one more than its blocks), plus one |
| u32 × (*n* - 1) | where each block's row starts, after this table, and (last) where the last ends |
| | the rows, one for each block from 1 |

A row is runs: a byte under 0x80 is that many copies of the byte after
it; a byte from 0x80 up is followed by that less 0x7f bytes as they are.
Unpacked, it's two bits for each block from block 0, the first block in
the low bits of the first byte: 3 seen, 0 not. Rows stop after their last
seen block.

## What Reluva does with it

The block the eye is in is the one whose outline holds it, between 1 m
below its lowest point and 30 m above its highest (where blocks lie over
each other, as a road over the Underground, the highest under the eye).
From there only the blocks its row sees are drawn: in the Underground 7
of London's 1,297 with anything to draw, in a London street 64, in a San
Francisco car park 84. Out of every block (flying above the city)
everything is drawn. The World page's
**V** turns this off and on; drawn both ways, the pictures from the
places above were the same.
