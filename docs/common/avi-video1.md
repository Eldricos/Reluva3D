# AVI / Microsoft Video 1

`common/video/avi_video1.h` reads a bounded, single-video-stream RIFF AVI and
decodes 8-bit indexed and 16-bit Microsoft Video 1 (`CRAM` / `MSVC`) into opaque RGBA frames.
This is the subset observed in the owner's Boggle Classic lid and hourglass clips, not a
general Windows codec host. It uses no OS codecs or runtime FFmpeg dependency.

## Layout and decoding

The reader walks size-delimited RIFF chunks, with little-endian lengths and
word padding, including `hdrl`, one `strl`, `movi` and nested `rec ` lists.
`avih`, `strh` and `strf` must agree on a single video stream, dimensions and
frame count. Stream timing is retained as an integer rate/scale pair. `00dc`
and `00db` packets are visited in file order; the index is not trusted for
addresses. Unknown chunks are skipped only within their containing bounds.

Frames contain bottom-up 4x4 blocks. A two-byte code selects:

- High byte `0x84..0x87`: skip the low ten-bit block count, retaining pixels
  from the previous frame. Zero and out-of-frame skips fail.
- High byte below `0x80`: a sixteen-bit pattern followed by two RGB555 colors.
  Bit 15 of the first color selects eight-color mode, with a color pair per
  2x2 quadrant. Pattern bits choose between the pair's colors. The color flag
  is not a color component.
- Other high bytes: a solid RGB555 block, ignoring bit 15.

Blocks advance left to right, then upward. RGB555 components are expanded by
bit replication to eight bits, with alpha 255. A two-byte zero packet-end
marker is accepted after a complete frame. Delta frames start with previous
pixels; the initial image is black. Output is cleared on failure, including
when a later packet is malformed.

## Bounds and unsupported forms

Limits: 64 MiB input, 512 frames, 2048 pixels per dimension, 128 MiB decoded
RGBA and eight nested list levels. Dimensions must be positive multiples of
four. Sizes, padding, frame counts, formats and stream start/timing fields are
validated before image allocation. A second stream list, audio/interleaved
streams, palette-change commands, Cinepak and OpenDML/multi-RIFF forms are not
supported. Unsupported data fails explicitly rather than selecting a codec
from filenames or executing bundled libraries.

## Evidence and verification

The implementation was written independently from observed AVI headers and
controlled block decoding experiments; no decoder source was consulted. Source
file identities are in Boggle's [install manifest](../plans/boggle-1997/install-manifest.tsv):
`Anims/ClsO1All.avi` and `Anims/ClsM1All.avi`, each 640x480, 55 frames, nominal
30/1 fps. Their use and application-controlled playback cadence are documented
in [Classic presentation](../engines/boggle/presentation.md).

All 110 decoded frames were compared against the preinstalled
[FFmpeg](https://ffmpeg.org/) 7.1.5-0+deb13u1 command-line decoder, used only
as an external research reference (the executable reports GPL-2.0-or-later). All 33,792,000 RGB555 color samples match. The unused high
bit of FFmpeg's RGB555 output was masked for comparison; it carries no color.
The reference tool's own license applies to that external tool; no FFmpeg code
or decoded frames enter the repository or native runtime.

`common.unit.avi_video1` uses entirely invented chunks and pixels. It covers
solid, two/eight-color, quadrant and bottom-up decoding, delta skips, truncation,
invalid dimensions/counts, bad codecs, decoded-size limits and failure clearing.
The optional Boggle presentation survey records only counts and checksums;
reference dumps and captures remain outside Git.

## Indexed frames

The 8-bit path reads the bounded RGBQUAD palette following the DIB header,
using `biClrUsed` or 256 entries. Every decoded index is checked against the
palette. Skip blocks retain prior indices. A pattern with high byte below
`0x80` carries two one-byte indices; high bytes `0x90..0xff` carry eight,
one pair per quadrant. Other non-skip codes fill a block with the low-byte
index. Frames remain bottom-up with opaque output alpha. Palette changes
inside the movie are rejected rather than silently ignored.

Format facts were checked against Mike Melanson's
[Description of the Microsoft Video-1 Decoding Algorithm](https://multimedia.cx/video1.txt),
v1.2 (2003), published under GNU FDL 1.2 or later without invariant sections.
Only format facts were used; no specification prose or decoder code was copied.
All 77 frames of `ClsO1Tim.avi` / `ClsM1Tim.avi` match the external FFmpeg
7.1.5 reference in RGBA. Invented unit fixtures exercise both indexed pattern
modes, solid colors, delta skips, palette bounds and truncated color data.
