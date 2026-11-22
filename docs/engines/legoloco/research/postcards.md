# LEGO Loco: postcards, a first look (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026. Only three postcards were looked at, so this is a start
for when postcards are worked on (the plan's sixth phase), not a format.

## Where they are

- **The CD:** `art-res\POSTBAG\Easter\<language>\*.crd`, 1,296 files in
  nine language folders (`Dan`, `Dut`, `Eng`, `Fre`, `Ger`, `Ita`,
  `Nor`, `Spa`, `Swe`), named by seven digits (`0910000.crd`) **seen**.
- **An install** adds `POSTBAG\ALBUM`, `AlbIndex`, `ATT_IN`, `ATT_OUT`,
  `SORT\BAG`, `SORT\IN` and `SORT\OUT`: the album, and cards coming and
  going **guess**.
- **The resource file's `post` folder** (155 entries) holds the postcard
  screens' pictures and their definitions, and `post\edit\colour.dat`,
  rows of three numbers **seen**.

## What one holds

The three English `Easter` cards looked at (`0910000.crd` to
`0910002.crd`) are 920 bytes each **seen**:

- a header whose unused bytes are `0xCD`, the fill Visual C++'s debug
  heap leaves in memory it hasn't written: a structure saved straight
  from memory, with whatever was in its padding **guess**;
- a name at `0x21`, `Skeleton` in the first **seen**;
- from about `0x90`, six-byte groups starting `69 04` (1,129) **seen**:
  the card's stamps or pictures, each a resource number and two more
  numbers **guess**.

## Code to start from

`loco.exe` sends files to other players in pieces of up to `0x7fdc`
bytes, with message `0x3fc`, at `0x439df0` **seen**: likely how
postcards travel over the network **guess**.
