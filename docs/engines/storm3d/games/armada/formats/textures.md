# Star Trek: Armada: textures and pictures (`.tga`, `.bmp`)

> **Status:** decoded · **Code:** `src/engines/storm3d/pages/image_page.cpp` (the viewer), `src/image/image.cpp` (reading) · **Checked on:** the English retail 1.0 install's 2,320 pictures · **Confidence:** layout confirmed; how the game picks between them inferred

## Overview

The models' and effects' textures are TGA files in two folders, and the
interface's pictures are BMPs:

| Where | Files | What |
|---|---|---|
| `Textures/RGB/` | 991 TGAs | Textures in full colour, 24 or 32 bits |
| `Textures/Index8/` | 1,078 TGAs | Textures in 256 colours, each with its own palette |
| `bitmaps/`, `bmp/`, `bzn/`, `splash.bmp` | 251 BMPs | The front end's screens (209), the installer's (13), each map's picture (28), and the splash screen |

`Textures/RGB/` also holds `StarTrek.pal` (768 bytes, the size of a
256-colour palette, but not the same as the install's `Armada.pal`) and `pspbrwse.jbf`, Paint Shop Pro's browse file,
left in by the developers.

## Layout

### TGA

Standard Truevision TGA files, uncompressed:

| Kind (image type) | Bits | Files |
|---|---|---|
| Colour-mapped (1), a 256-entry palette of 24-bit colours | 8 | all 1,078 in `Index8/` |
| True colour (2) | 24 | 660 in `RGB/` |
| True colour (2), with alpha | 32 | 331 in `RGB/` |

Most are stored bottom row first; 101 of `RGB/`'s are top row first (bit 5
of the header's last byte). The usual sizes are square powers of two,
from 8×8 to 256×256; 40 are 400×200 (`B1COL1.TGA`...).

### Mipmap levels

Beside 186 textures are smaller copies of the same name ending `.1` to
`.4`: `Bresear.tga` (256×256) has `Bresear.1` (128×128) to `Bresear.4`
(16×16). Each is a TGA of the same kind, the texture halved once for each
level; 142 textures go to `.4`. The executable calls them mipmaps
(`MIPMAP: '%s' - Too many mipmap levels`).

Exceptions:

- Seven in `Index8/` (`Z_mama`, `Zcardbat`, `Zclonfac`, `Zentity`,
  `Zferengi`, `Zjembat`, `Zjemyard`): the `.tga` is 32×32, but `.1` to
  `.3` are 128, 64 and 32 pixels across, as if halved from a 256×256
  texture that was then replaced.
- `particle.2` to `.4`, in both folders, have no `particle.tga`.
- Five files in `Index8/` have no extension (`Xspark01`, `Xpl1_00`...) and
  `RGB/` has `Fpod.6` and `Fpod.56`: TGAs all the same.

## Behaviour

- **Which folder.** Run in Reluva's emulator at the 640 × 480 display
  mode it chose, the original read every texture from `Textures\RGB\`,
  none from `Index8\` ([systems/hosting.md](../systems/hosting.md)). Its
  message `Couldn't find index texture '%s', using rgb.` suggests
  `Index8\` comes first in another mode *(inferred)*: an 8-bit one, by the
  name.
- **Mipmap levels, as the original reads them.** After each `.tga` it
  opens `.1`, `.2` and on until one is missing (`MBuild.tga`, `.1`, `.2`,
  then no `.3`); seen running, as above.
- `texture.dat` (8 bytes: `02 00 00 00 ef 89 b5 96`) and the setting
  `texture_detail` are presumably the chosen texture detail *(guessed)*.

## In Reluva

Pictures are read with the shared decoder (`image::decode`,
`src/image/image.h`), which reads every kind above.

- **The data viewer's Images page** (`ImagePage`,
  `src/engines/storm3d/pages/image_page.h`) lists every TGA and BMP in the
  install, and the other files in `Textures/`, by folder, each texture
  followed by its mipmap levels.
- `reluva-tool storm3d image-check <game folder>` decodes every picture
  the page lists and counts them by size.
- `reluva-tool storm3d image <game folder> <name> <out.png>` writes one as
  PNG.

## Sources and checks

- **Where the facts come from:** the TGA headers of every texture, counted
  by kind and checked level by level against their textures' sizes; the
  messages in `Armada.exe`'s strings.
- **How it was checked:** `image-check` decodes all 2,320 pictures; a
  palette texture, a full-colour one with alpha, a mipmap level and the
  main menu's backdrop were looked at as PNGs, and are right way up and in
  their colours. The engine test shows textures and a mipmap level in the
  viewer.

## Open questions

- Which of the two folders the game uses when, and what `texture.dat`
  holds.
- What the seven odd mipmap sets mean for the game: whether it uses the
  `.tga` or the levels.
