# Classic word-list handwriting

The installed-artwork Classic view now draws submitted words and their points
using `Fonts/KPSI.TTF` when available. Accepted words retain dark ink; words
pending dictionary review retain red ink. Long words are fitted to the existing
word panel with an ellipsis. This is a native 18-pixel ASCII presentation, not
a claim of matching original Windows font sizing, weight, hinting or layout.
Other labels and the prototype view keep Reluva's built-in font.

**Classic handwriting font** (`classic_handwriting`, default `1`) controls this
optional use. Set it to `0` before launch for the built-in word-list font.
Copies without KPSI also use that font. Corrupt or unsupported installed KPSI
reports an error when enabled; disabling the setting permits the built-in view.
New imports add KPSI when present: 53 files for the supplied full set. Re-import
from the full install or CD to add it to an earlier 52-file copy.

The [shared ASCII TrueType reader](../../common/font-truetype.md) parses and
rasterizes outlines independently. It does not install fonts, invoke Windows
font APIs or execute hint programs. Font files and decoded masks are runtime
data and remain outside Git.

## Evidence and limits

The supplied font is identified in the [install manifest](../../plans/boggle-1997/install-manifest.tsv).
Read-only inspection of executable SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`
finds the family string `KPSI Handwriting Font` at VA `0x489f5c`.
Player setup near `0x4197e4` and `0x41980c` passes that family into font creation
at `0x44a290`; the font-construction sequence includes `0x44a464..0x44a489`.
This establishes original use of the installed family, not an exact match
between every original control and native font size. Native list integration
and spacing remain subject to VM comparison.

All 95 printable ASCII entries map to simple outlines in the supplied font.
The current reader intentionally excludes composite ASCII glyphs and non-ASCII
text; broader language/font support remains future work. The detector's existing
TARA marker remains in imports, but TARA and Nelson rendering are not enabled.

`reluva-tool boggle font-check <folder>` validates KPSI when present and reports
only font/glyph counts, total advance and a coverage checksum. Fresh-import
checks run the same survey on the copied resources. Synthetic integration tests
cover a missing font, enabled handwriting, word fitting, pending-word color,
explicit disable and corrupt-font recovery. The shared reader tests cover curves,
winding, packed coordinates and malformed data. Windows appearance comparison
is still needed for this pass.

The supplied install passes all 23 Boggle checks, including the external
53-file import and font survey. The normal full suite passes 34 checks and
skips 22 unconfigured optional data checks. Reader/engine tests and the actual
font survey pass ASan/UBSan (LeakSanitizer disabled for the execution tracer).
The shared font tests and coverage check also pass with Boggle disabled. All
737 original install files still match the recorded SHA-256 manifest.
