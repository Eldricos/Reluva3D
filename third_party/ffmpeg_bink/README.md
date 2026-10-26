# FFmpeg's Bink tables

The data tables of FFmpeg's Bink video decoder, from
`libavcodec/binkdata.h` at tag `n7.1` of <https://github.com/FFmpeg/FFmpeg>:
the block scan order, the 16 Huffman trees' codes and lengths, the 16 run
fill patterns and the intra and inter quantiser matrices. They're
unchanged, but put in a C++ namespace as `inline constexpr`. Revision b's
tables are left out.

Licensed under the GNU Lesser General Public License, version 2.1 or later
(see `LICENSE`; copyright Konstantin Shishkov, 2009), which allows them
in Reluva under the GPL 3.0. Reluva's decoder,
`src/common/video/bink_video.cpp`, is its own code. FFmpeg's decoder
(`bink.c`, `binkdsp.c`, `binkaudio.c`) was its reference for what the
published description of the format leaves out. See
`docs/formats/bink.md`.
