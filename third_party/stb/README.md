# stb_image, stb_truetype

An image decoder and a TrueType font rasteriser by Sean Barrett,
<https://github.com/nothings/stb>, dual-licensed MIT or public domain (see
`LICENSE`). Only `stb_image.h` (v2.30) and `stb_truetype.h` (v1.26) are
included, unchanged, from commit
`f58f558c120e9b32c217290b80bad1a0729fbb2c`. Reluva uses `stb_image.h`
through `src/common/image.cpp` (TGA, JPEG, PNG and BMP decoding) and for
its deflate decoder in `src/common/zip.cpp`, and `stb_truetype.h` through
`src/common/truetype.cpp` (games' own fonts).
