# Animations and sprites (.RLE)

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

## Animations and sprites (`.RLE`)

```
file:   8 bytes  00 00 01 01 0A FF 00 00
        u16      frame count
frame:  u8       key / flags (purpose not confirmed)
        u16      width, height, x, y
        12 bytes unknown
        u32      length of the row data
        row data
```

The row data is a byte stream. The top two bits of each byte select the operation
and the low six bits are a count *n*:

| Bits | Operation |
|---|---|
| `00` | copy the next *n* bytes as pixels |
| `01` | start a new row, then skip *n* transparent pixels |
| `10` | repeat the next byte *n* times |
| `11` | skip *n* transparent pixels |

The last row may be followed by one more "new row" byte.
