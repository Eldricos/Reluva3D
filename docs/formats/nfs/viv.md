# Need for Speed: `.viv` archives

Status: **implemented** (`src/engines/nfs/viv.cpp`), and checked on all
115 retail archives in the NFS III install and on its CD.

Sources: the layout as the NFS modding community describes EA's "BIGF"
archives. Each field was confirmed on the retail files with a survey
script, October 2026: magic, sizes, the first file's offset, alignment
and order.

## Layout

The header and directory are **big-endian**.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `BIGF` |
| 4 | u32 | the archive's size (equal to the file's in all 115) |
| 8 | u32 | number of files |
| 12 | u32 | offset of the first file's data (just after the directory, rounded up to 4) |
| 16 | ... | directory |

Each directory entry is:

- u32 offset;
- u32 size;
- the name, ended by a 0.

The entries are in the same order as the files' data. Files start on
4-byte boundaries, with 0 to 3 bytes of padding between them.

## What they hold in NFS III

- **Cars** (`GameData/CarModel/<car>/car.viv`):
  - `carp.txt` (performance);
  - `car.fce` (model);
  - `car00.tga` (skin);
  - `fedata.*` (front-end text in 7 languages);
  - `fedata.fsh`;
  - the engine sounds (`car.bnk`, `ocar.bnk`, `ocard.bnk`, `scar.bnk`);
  - `dash.qfs` (cockpit).

  Traffic cars hold only the first three.
- **Speech** (`ipspch?.viv`): one `.bnk` per voice.
- **Front end:** `trkgo.viv` holds `go*.fce` and `go*.tga`, track models
  for the menus.
