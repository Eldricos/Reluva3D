# Star Trek: Starfleet Command II: models (`.mod`)

> **Status:** partial · **Code:** `src/engines/sfc/formats/model.cpp` · **Checked on:** English retail 1.0, all 102 files · **Confidence:** layout confirmed; meanings inferred unless marked

## Overview

`Assets/MODELS/<name>/` holds a ship's, base's or asteroid's model, its
broken version (`<name>_brk.mod`) for when it's destroyed, and their
textures as 8-bit BMPs. The folder's name is the ship's: its race's
letter, then its class (`fca`, a Federation heavy cruiser; `gbs`, a
Gorn base station). A model has its named points (where weapons and
systems are), its materials, and one mesh for each level of detail.

## Layout

All numbers little-endian; floats are 32-bit IEEE.

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `MDLS` |
| 4 | 4 | the size of the rest of the file |
| 8 | 4 | 512 in every model |
| 12 | 4 | float: the distance of the farthest vertex from the origin, to two decimals (compared on 14 models) |
| 16 | 4 | the number of levels of detail: 1 to 4 |
| 20 | 24 | six floats: 5.0 in every model, then 0, then up to four rising numbers, zeros after them (100, 200, 300, 400 in 21 models; 180, 350 in 19; 1800, 5000 in 12) which may be the distances at which the levels of detail change, though their count doesn't always match the levels' *(guessed)* |

### Chunks

From 44 on, chunks: a four-letter tag, a 32-bit length, then the body,
padded with zeros to a multiple of four bytes (STRS is the one that
needs it). Every model has STRS, MATS and one MOD0 for each level; most
have PNTS.

| Tag | What |
|---|---|
| `STRS` | names, each ending in a zero byte; other chunks name them by their offset here |
| `PNTS` | 16 bytes a point: x, y, z floats, and its name's offset (`hardpoint5`) |
| `MATS` | the materials, below |
| `MOD0` | a level of detail, below; finest first |

### A material

Each starts with 32 bits of flags. Bit 0x20 says it has a texture; then
eight words follow, otherwise six.

| Words after the flags | Untextured (six) | Textured (eight) |
|---|---|---|
| 1 | colour, 0xFFFFFF in every model | colour, 0xFFFFFF in every model |
| 2 to 5 | floats 1.0, 1.0, 0, 0 | 0 in every model |
| 6 | 0 | the texture's name's offset (`fca_1_1`, a `.bmp` in the folder) |
| 7 | | 0 |
| 8 | | the self-illumination map's name's offset (`fca_1_1i`) |

Flags seen: 0, 2, 3, 4 and 5 untextured; 35 and 37 textured. The
triangles count materials from 0, in this order.

### A level of detail (`MOD0`)

The vertex count and the triangle count, 32 bits each, then a `VTXS`
chunk (x, y, z floats for each vertex) and one `POLY` chunk for each
triangle:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | corners: 3 in every POLY |
| 4 | 4 | the material |
| 8 | 12 | the face's normal |
| 20 | 28 x 3 | each corner: its vertex (32 bits), three floats (unknown: not of unit length, so not a normal), u and v, and 32 bits that are 0 in every corner |

A corner's vertex index uses only its low 16 bits for the vertex. In
the broken models the high 16 bits are 1, 2, 3 or more; in the others,
0. No triangle mixes two values *(inferred: the pieces a ship breaks
into)*.

## In Reluva

- **Reading it:** `Model` (`src/engines/sfc/formats/model.h`).
- **The data viewer:** the Models page draws each model with its
  textures, lit by a fixed light, each level of detail in turn, and marks
  its named points. It turns z round, as Direct3D's coordinates are
  left-handed and Reluva's renderer's right-handed *(guessed: that the
  game doesn't mirror them itself isn't checked)*; it draws both sides of
  every triangle, and not the illumination maps.
- **Commands:** `reluva-tool sfc models <game folder>` reads every model
  and lists the textures it names that the folder doesn't have.

## Sources and checks

- **Where the facts come from:** the files themselves: the chunks' tags
  and lengths, the counts against the chunks they count, the materials'
  flags against their lengths, the names they point to against the BMPs
  beside them. Taldren's model viewer and 3ds Max plug-ins, published on
  ModDB, say only that models for Volume II also have self-illumination
  maps; no layout of the format was found published.
- **How it was checked:** all 102 models read to their last byte, each
  level's counts matching its chunks, every triangle's vertex and
  material within range; models drawn by the data viewer with their
  textures in place and their points on their hulls. Four illumination
  maps are named and not in the install (`hdn_1_1i` among them).
- **Unit test:** `sfc.unit.formats` (model, model bad material), on a
  model built in the test.

## Open questions

- The corners' three floats; the header's six floats; the material flags'
  bits but 0x20; the untextured materials' 1.0s.
- Which axis is the ship's forward: it isn't the same one in every model.
- How the game uses the illumination maps, and the broken models'
  pieces.
