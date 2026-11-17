# stb_image, stb_image_write, stb_truetype

An image decoder and encoder and a TrueType font rasteriser by Sean
Barrett, <https://github.com/nothings/stb>, dual-licensed MIT or public
domain (see `LICENSE`). Only `stb_image.h` (v2.30), `stb_image_write.h`
(v1.16) and `stb_truetype.h` (v1.26) are included, unchanged, from commit
`f58f558c120e9b32c217290b80bad1a0729fbb2c`. Reluva uses `stb_image.h`
through `src/image/image.cpp` (TGA, JPEG, PNG and BMP decoding) and for
its deflate decoder in `src/common/zip.cpp`, and `stb_truetype.h` through
`src/graphics/truetype.cpp` (games' own fonts), and `stb_image_write.h`
through `src/image/image.cpp` (JPEG encoding, for Jedi Outcast's saved
games' pictures).
