# Printable ASCII TrueType outlines

`font::TrueTypeAscii` reads a bounded TrueType font and caches coverage masks
for printable ASCII at an 8–48 pixel em height. It is original Reluva code,
written from public binary-format facts, with no platform font service or
third-party rasterizer dependency. It never installs fonts or executes font
instructions.

The reader accepts the sfnt 1.0 directory, `head`, `maxp`, `hhea`, `hmtx`,
short/long `loca`, Unicode format-4 `cmap` and simple `glyf` outlines. It supports
signed word and packed short coordinate deltas, repeated flags and implied
on-curve points between quadratic controls. Composite glyphs referenced by
ASCII, incomplete ASCII maps, CFF, collections and variable fonts fail explicitly.
Unreferenced glyph outlines and hint tables are not interpreted. Font checksums
are not an authenticity check.

The renderer flattens quadratic contours with bounded subdivision, then uses
nonzero winding and 4x4 coverage samples. Geometry and intersections use integer
coordinates at 1/64 pixel resolution. Advance widths and left bearings come
from horizontal metrics. The caller supplies a baseline, color and width;
drawing clips to both the surface and that width. Byte values outside printable
ASCII use `?`; this API does not implement Unicode shaping or kerning.

Limits are 4 MiB input, 64 tables/maps, 8192 glyph IDs, 4096 cmap segments,
128 contours and 2048 points per decoded glyph, 4096 flattened edges per glyph,
65536 edges across the ASCII cache and 256x256 coverage pixels per glyph.
Directory spans, duplicate/overlapping tables, glyph offsets, mapping ranges,
instruction lengths and coordinate bounds are checked before use. Failure clears
the previous cache. Font bytecode is skipped using checked lengths.

## Sources and verification

Format facts were checked against Microsoft's OpenType specification:
[head](https://learn.microsoft.com/en-us/typography/opentype/spec/head),
[maxp](https://learn.microsoft.com/en-us/typography/opentype/spec/maxp),
[hhea](https://learn.microsoft.com/en-us/typography/opentype/spec/hhea),
[hmtx](https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx),
[loca](https://learn.microsoft.com/en-us/typography/opentype/spec/loca),
[cmap](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap) and
[glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).
No specification prose, font outlines or renderer implementation was copied.

`common.unit.font_truetype` assembles entirely invented sfnt tables and shapes:
a square with an opposite-winding hole, quadratic curves and an all-off-curve
contour. It checks both offset/metric forms, packed/repeated flags, coverage,
clipping, all file truncations and malformed bounds. No font bytes are embedded.

For Boggle's supplied KPSI font, all 95 rounded advance widths at 18 pixels match
an external FontTools metadata inspection (624 pixels total). This is a metrics
check, not a pixel match to Windows GDI: hinting, kerning, font synthesis and
original text placement are not reproduced. FontTools was used only as an
external reference, without consulting or copying its source. Original fonts,
reference output and captures remain outside Git; game-data records contain
only counts and checksums.
