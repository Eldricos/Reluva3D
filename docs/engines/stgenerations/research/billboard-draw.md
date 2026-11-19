# Billboards (LV2 type 5) and translucency in the column renderer (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: **H** = read directly from code, **M** = code plus data, **L** = guess.
Sample data: verid1 game.lv2.

## 0. Summary

- The billboard flags word is **record +0x6C** (u32). It is the same offset as for actors and walls. H
- There are three column routines, chosen by **item+0x10 ("method")** (H):
  - **0**: keyed. 0xFF is skipped; everything else is written (shaded).
  - **3**: fully opaque. Every texel is written, including 0xFF (palette 255 = magenta 255,0,255).
  - **1**: keyed plus translucent. 0xFF is skipped. Texels **0xF0..0xFE** are blended with the framebuffer through a 15×256 table. Other texels are written (shaded).
  - (2 is only for type-7 3D boxes, 0x4C8EF0.)
- Translucency is **only** for palette indices 0xF0..0xFE, and **only** when record flag **0x8** is set.
  For a billboard/actor this also needs flag 0x4. Without 0x8, indices 0xF0..0xFE are plain opaque
  colours: (200,200,160), (170,200,210), (212,169,188) and so on. These are light whites/pinks, i.e. the
  "pink or white dots". H
- The blend is a **50/50 average** of the two palette colours, mapped back to the nearest palette entry.
  The table is built at level load from the LV2 palette. H

## 1. Billboard → draw item

Per-frame list builder: **0x4C7BB0** (called from the scene builder 0x4C0840 → 0x4C7D00 walls, 0x4C7BB0
billboards, 0x4C7AB0 actors, 0x4C79C0 boxes, 0x4C77E0 other).
- It walks the list `*0x57A0E8` (next = +4). This is the visible-billboard list. Flag 0x100 is toggled by
  show/hide 0x4B5FC0, which then relinks through 0x4B5120 (M).
- Zone test: rec+0x3C & *0x57D99C || rec+0x40 & *0x57D9A0. Then camera transform 0x4B5370 and the
  depth < rec+0x5C test. H
- Animation (0x4C7C1C): if u16 +0x86 (frame count) > 1:
  `f = ((now(*0x57A034) − u32 +0x80) * u16 +0x84) >> 16 + u16 +0x88`, wrapped mod count (the phase in +0x88
  is re-based when it wraps). Then `rec+0x10 = texture[*0x5744B0 + 4*(u16 +0x8A + f)]`.
  So +0x80 = start time, +0x84 = rate, +0x86 = count, +0x88 = phase, +0x8A = first texture (LV2 chunk 13 id). H
- The item (0x7C bytes, in the array `*0x575EFC`) is filled by the shared projector **0x4C5820** (H):

| item field | value | source flag |
|---|---|---|
| +0x0C | 4 (item type: sprite/billboard) | |
| +0x10 | method: **1 if flags & 0x4, else 3** (0x4C5955..0x4C596C) | 0x4 |
| +0x18 | (flags & 4) >> 2 = "use per-column class tables" | 0x4 |
| +0x58 bit1 / bit2 | flags 0x40 / 0x80 (`flags >> 5` merged into bits 1,2; 0x4C59C9..0x4C59EE) | 0x40, 0x80 |
| +0x58 bit0 | on screen | |
| +0x54 | light: 0 if flags & 0x10, else rec+0x68 (byte) | 0x10 |
| +0x24/+0x38 | u start / du; with flags & 0x20 u starts at the right and du is negated (mirror) | 0x20 |
| +0x1C | texture (rec+0x10); +0x20 = the record itself | |

Flag bits that the draw path does **not** read in 0x4C5820: 0x1, 0x2, 0x8, 0x100, 0x200.
- **0x2 and 0x8 are read later, per column, through item+0x20 → rec+0x6C**, in 0x4C0AD0 (§2). H
- 0x100 = visible (list membership; HIDE clears it). M
- 0x200 is not used by any draw code I found. L
- 0x1 is "not drawn" for actors. Billboards don't test it on this path (L).
- 0x400000 (on 22 billboards) is not read by the draw path.

## 2. Column clipper 0x4C0AD0: the per-column choice of method

0x4C0940 calls 0x4C1320 (build the column's item list), then **0x4C0AD0**. 0x4C0AD0 walks the column's
items near to far, clips each against the open window [top *0x55231C, bottom], and emits 32-byte spans into
`*0x552334`. H

Texture header (chunk 13; set up in 0x4C4F20):
- u16 +0xC = id, u16 +0xE = column count W, u16 +0x10 = rows H, u16 +0x12 bit0 = "no column tables".
- The pixels are column-major at +0x14.
- Then three W-byte tables follow: t0 (first opaque row), t1 (last opaque row), t2 (column class).
  t0 and t1 are scaled to 0..255, i.e. row×256/H. They are used as `t<<15` in the 0x7FFFFF v space.
- Column class t2, checked against the data (all chunk-13 textures in verid1, every column) (M, 0 mismatches):
  - **0** = column all 0xFF.
  - **1** = has 0xFF holes inside.
  - **2** = one contiguous opaque run (may touch the ends).
  - **3** = fully opaque, no 0xFF.
- Textures with +0x12 bit0 use static tables: t0 = 0, t1 = 0xFF, t2 = 3 (0x4BFEB0).

Logic per column, only when item+0x18 != 0 (i.e. flag 0x4) (0x4C0B59..0x4C0E10, H):
```
cls = t2[col]
if cls == 0:                       skip this item in this column
elif cls == 3:                     if !(flags & 8): method = 3      (opaque; fine, no 0xFF)
elif cls == 2 and (flags & 2):     clip span to rows t0..t1 (drop the 0xFF ends)
                                   if !(flags & 8): method = 3
else (cls 1, or cls 2 w/o flag 2): if !(flags & 8): method = 0      (keyed)
```
- `method` is written back to item+0x10, so it persists to the next column.
- With flag 8 it is never overwritten and stays at the initial value.
- So flag **0x2 is only an optimisation**: it allows the opaque routine on single-run columns after trimming
  the transparent ends. It does not change the image.
- **Flag 0x8 = translucent.** It keeps method 1. H
- Effective method:
  - billboard/actor: `(flags&4) ? ((flags&8) ? 1 : keyed/opaque per column) : 3 (opaque, 0xFF drawn!)`.
  - wall: see §5.

Occluder bits (item+0x58 bits 1/2, i.e. record flags 0x40/0x80), at 0x4C0E51 (H for the code, M for the
meaning). The span is [ys, ye):
- method ≥ 2 (opaque): bit1 → window bottom = ys; bit2 → window top = ye.
- method 0/1 (see-through): bit1 → window bottom = ye; bit2 → window top = ys.

So 0x40 means "nothing farther is visible below this item" and 0x80 means "nothing farther is visible
above". With an opaque method the item's own span is included. These are portal/occlusion hints
(walls use them heavily). They don't change how the item's own pixels are drawn.

The span records the method (+0x1C) and the shade table `*(0x575D80 + 4*light)` (+4). The final paint is
**0x4C7420**, which runs each column's spans in reverse order (far to near). The low-detail variant is
0x4C6D60 and the MMX variant is 0x4C7750; they use the same methods.

## 3. Column routines (span painter 0x4C7420)

| method | unshaded (shade ptr == identity 0x575DC0) | shaded | behaviour |
|---|---|---|---|
| 0 | 0x4C6F80 | 0x4C7040 (0x4C7610 MMX, 0x4C6C60 low) | `if s != 0xFF: dst = shade[s]` |
| 1 | 0x4C7130 | 0x4C7280 (0x4C6CD0 low) | `if s == 0xFF: skip; elif s >= 0xF0: dst = blend[(s-0xF0)*256 + dst]; else dst = shade[s]` |
| 3 | 0x4C6DF0 | 0x4C6E90 (0x4C6C00 low) | `dst = shade[s]` (no test at all) |
| 2 | 0x4C8EF0 | – | 3D box (type 7) |

`blend` = `*0x552360` = 0x57AFC0 (BSS, 15×256 bytes). No other palette index is special:
- index 0 is an ordinary colour;
- only 0xFF (transparent) and 0xF0..0xFE (translucent, method 1 only) are special. H

### Shading vs blending (H)
- A translucent texel is **not shaded**: the blend table is applied instead of the shade table.
- The background `dst` is the framebuffer byte, which was already shaded/fogged when it was drawn (far to
  near). So the blend is applied after shading of the background, and shading is never applied to the
  source.
- Opaque texels of a method-1 item are shaded normally.

## 4. Blend table formula (builder 0x4BD620, called once per level load at 0x4B56F9, after the palette)

Palette = LV2 chunk type 1 (1024 bytes, 4 per entry, 8-bit components, 4th byte 0), copied to 0x57D430.
```
for k in 0..14:                     # source texel s = 0xF0 + k
  S = pal[0xF0 + k]
  for b in 0..255:                  # background index (current framebuffer pixel)
    T = ((pal[b][c] + S[c]) >> 1 for c in 0..2)     # floor average, per byte
    best = argmin over j = 0..255 of sum_c (T[c] - pal[j][c])^2
           # initial best distance 0x10000000, strict '<': ties -> lowest j; j includes 0xF0..0xFF
    blend[k*256 + b] = best
draw: dst = blend[(s - 0xF0)*256 + dst]
```
- Row = source texel, column = background. The operation is symmetric 50/50, not additive.
- The channel order doesn't matter (the same op on all three bytes).
- Palette fades at runtime don't rebuild it.

### Shade tables (for reference; builder 0x4BFC60, H)
- `*0x575D80[0]` = identity (0x575DC0).
- Levels k = 1..7 (tables at 0x575580 + 256k):
  `T = pal[i] + ((fog - pal[i]) * k >> 3)` per channel (arithmetic shift), then the same nearest-colour
  search.
- The fog colour comes from the args (>>8).
- Index = signed light byte (item+0x54).

## 5. Walls (projector 0x4C7D00, item fill at 0x4C8095..0x4C80F3, H)

- Wall item: method = **3, or 1 if wall flags & 0x8**. +0x18 = (flags&4)>>2, +0x58 bits 1/2 = flags 0x40/0x80,
  light = rec+0x68.
- The same column-class code reads rec+0x6C bits 0x2/0x8. So for walls:
  - **0x8 = translucent** (method 1, whether or not 0x4 is set);
  - 0x4 = keyed per column;
  - neither = opaque.
- verid1: no wall has 0x8 (0x2 on 859, 0x4 on 199, 0x40 on 1939, 0x80 on 658 of 2815).
- Small asymmetry: a billboard/actor with 0x8 but **without** 0x4 gets method 3 (fully opaque). A wall with
  0x8 alone is translucent.

## 6. Applied to the sample billboards

| flags | texture(s) | engine result |
|---|---|---|
| 0x30C (×36) | 143..147 (steam), 529.. | **method 1**: 0xFF skipped, texels 0xF0..0xFE blended 50/50 with the background (tex 143: 8508 of 8752 opaque texels are ≥ 0xF0, mostly 249 = (224,224,180)) |
| 0x306 (×68), 0x400306 | 168..183, 84, 115, 133, 134, 209.. | keyed (method 0) or trimmed opaque (3); **no translucency**. Any 0xF0..0xFE texels are drawn as those palette colours. Tex 168: 282 such texels, so a few light dots are authentic |
| 0x304 (×12) | 2..5 | keyed (method 0 for class-1/2 columns, 3 for class-3 columns), **not** an opaque rectangle. Tex 2 has only 1932 non-0xFF texels out of 32768 |

**For a renderer that reproduces this:**
- Treat 0xFF as transparent always, unless (actor/billboard without 0x4): then draw it as palette 255.
- If flags & 0x8 (and 0x4 for billboards): `out = blend[s-0xF0][dst]` for s in 0xF0..0xFE.

Scripts and disassembly dumps for this note were working files, not kept.
