# Windows bitmap resources

The independently written `readBitmapResource` reader decodes the installed
language DLL's packed RT_BITMAP DIB data without loading executable code or
calling GDI. These resources omit a BMP file header: a 40-byte
`BITMAPINFOHEADER` precedes an optional palette and padded pixel rows.

Supported inputs are uncompressed BI_RGB with one plane, positive width, signed
nonzero height, and either 8-bit indexed pixels or 24-bit BGR pixels. Positive
height stores rows bottom-first; negative height stores them top-first. Row
stride is rounded up to four bytes. Indexed palettes use four-byte RGBQUAD
entries, with `biClrUsed` selecting 1..256 entries or zero selecting 256.
Reserved palette bytes are ignored; output is opaque RGBA, not cursor AND/XOR
mask data. True-color resources must not declare palette entries.

Input is capped at 16 MiB and each dimension at 2,048 pixels. Header size, plane
count, compression/depth, palette bounds/important count, declared image size,
exact payload length and every palette index are validated. BI_RGB image size
may be zero or the exact padded row byte count. Unsupported formats, truncated
inputs, out-of-range indices and trailing bytes fail with empty output. Other
DIB headers, bitfields, RLE, embedded image formats and 1/4/16/32-bit pixels are
outside this reader's supported slice.

Microsoft's [BITMAPINFOHEADER](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
and [RGBQUAD](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-rgbquad)
format descriptions provide the public structure definitions. No external
implementation code was used. The source DLL identity is recorded in
[frontend research](../frontend.md#evidence-and-missing-file-correction).

## Supplied data and checks

Both the install and expanded CD language DLL decode four bitmap resources:
three indexed portraits and one true-color image, totaling 16,384 pixels.
An external check hashes resource ID, language, dimensions (each as a 32-bit
little-endian value), followed by decoded RGBA, in resource-directory order.
Both trees report `9a680ce7ff30f17a7624306aab61e1e3`.
Only this metadata/checksum is recorded; raw resources and checks remain outside
Git.

`boggle.unit.bitmap` uses invented indexed and true-color images to check row
order, four-byte padding, palette defaults/reserved bytes, exact colors, all
truncations and invalid headers/sizes/indices. It also checks roster resource
selection, role/empty-slot mapping, partial bundles and placement errors using
invented PE resources and dialog controls.
