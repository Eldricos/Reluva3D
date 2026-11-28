# stb_image, stb_truetype, stb_vorbis

An image decoder, a TrueType font rasteriser and an Ogg Vorbis decoder by
Sean Barrett, <https://github.com/nothings/stb>, dual-licensed MIT or public domain (see
`LICENSE`). Only `stb_image.h` (v2.30), `stb_truetype.h` (v1.26) and
`stb_vorbis.c` (v1.22) are included, unchanged, from commit
`f58f558c120e9b32c217290b80bad1a0729fbb2c`. Reluva uses `stb_image.h`
through `src/image/image.cpp` (TGA, JPEG, PNG and BMP decoding) and for
its deflate decoder in `src/common/zip.cpp`; `stb_truetype.h` through
`src/graphics/truetype.cpp` (games' own fonts); and `stb_vorbis.c` through
`src/audio/compressed.cpp` (Ogg Vorbis files, such as CD tracks ripped to
them).
