# SimCoaster: pictures (`.fsh`, `.tga`, `.png`, `.jpg`)

> **Status:** decoded · **Code:** `src/image/fsh.cpp`, `src/image/image.cpp` · **Checked on:** the US release's 10,344 pictures · **Confidence:** confirmed unless marked

## Overview

The game's pictures, loose and in its archives ([wad.md](wad.md)):

| Kind | How many | Where |
|---|---|---|
| `.fsh` | 7,283 | The models' textures, in every ride's, shop's and scenery's archive, and the front end's (`ui.wad`, 1,278) |
| `.tga` | 2,856 | More textures, the map's sprites (`Data/2dmap/`), the cursors (`Data/ui/cursors/`), the sky (`Data/Sky/skytop.tga`) |
| `.png` | 172 | The start-up screens, for each screen width and language (`Data/Init/<width>/`) |
| `.jpg` | 33 | The postcards' album (`Data/postcard/`) |

Most of the models' textures come twice: in a folder `textures` and in
one `stexture` beside it (267 and 268 folders), the same pictures smaller
in `stexture` (the tour ride's bird: 128×128 and 16×16): a set for a
lower detail setting *(guessed)*.

## Layout

### The `.fsh` banks

EA's picture banks, as the shared page describes them
([fsh.md](../../../../../common/image/fsh.md)), each of SimCoaster's
holding a single picture, with directory id `G231`:

| Blocks | How many |
|---|---|
| A compressed 8-bit picture (`0xFB`), a 24-bit palette (`0x24`), its name (`0x70`) | 3,450 |
| A compressed 8-bit picture, a 32-bit palette (`0x2A`), its name | 2,727 |
| A compressed 8-bit picture, a 24-bit palette | 410 |
| An 8-bit picture (`0x7B`), a 24-bit palette, its name | 374 |
| A compressed 8-bit picture, a 32-bit palette | 148 |
| An 8-bit picture, a 32-bit palette, its name | 97 |
| An 8-bit picture, a 24-bit palette | 74 |
| An 8-bit picture, a 32-bit palette | 2 |
| A compressed 8-bit picture, a 16-bit palette (`0x2D`), its name | 1 |

Every bank's size field is its file's. The name block's name is the
file's name without `.fsh`, for all but 55.

The 24-bit palettes' colours are R, G, B, not the B, G, R of Need for
Speed III's: read as B, G, R, the Arabian world's picture on the world
menu (`ui/stexture/arab4.fsh`) is ice blue and the Polar world's
(`water3.fsh`) sand, the ATM desk's wood (`woodentable.fsh`) navy, and
the cross in `mugshot.fsh` blue where the cancel buttons' is red; as
R, G, B each is the colour it should be. The 32-bit palettes are B, G, R, A, as in
Need for Speed III: their pictures (the Theme Park logo's gold, the
red cancel cross) come out right that way.

### The start-up screens

`Data/Init/<width>/` (640, 800, 1024 and 1280) holds each screen at that
width, in each language (`Welcome_English.png`, `welcome_american.png`,
`Legal_german.png`...). Seven at 800 (`Welcome_English.png` and six
others) end after their last image data, without the `IEND` chunk a PNG
ends with; their pictures are whole.

## In Reluva

The shared code reads them all: the banks (`src/image/fsh.cpp`), the
`.tga`, `.png` and `.jpg` files through stb_image
(`src/image/image.cpp`), which is given the missing `IEND` where a PNG
has none. `reluva-tool tpw picture-check <game folder>` decodes every
picture; `reluva-tool tpw picture-dump <game folder> <name> <out.tga>`
writes one out. The game-files test runs the first
(`tpw.gamedata.pictures`).

## Sources and checks

- **Where the facts come from:** the US release's pictures (November
  2026): every bank's blocks listed, and the `.png` files' chunks.
- **How it was checked:** all 10,344 pictures decoded with `reluva-tool
  tpw picture-check`, and some written out and looked at: a ride's
  feather texture, the welcome screen; the world menu's pictures, the
  logo and textures of both palette kinds, for their colours.

## Open questions

- Which of `textures` and `stexture` the game uses when.
