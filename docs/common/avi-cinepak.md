# Bounded AVI / Cinepak reader

`video::AviCinepak` decodes classic RIFF AVI with a Cinepak video stream at index
zero and optional PCM audio at index one. It returns owned top-down RGBA frames,
the AVI rational frame rate, and signed 16-bit interleaved audio. It uses no
platform codec, original executable code, FFmpeg library or new dependency.
The existing 16-bit Video 1 reader remains separate and unchanged.

## Sources and independent implementation

Format facts came from [Dr. Tim Ferguson's 2001 Cinepak stream description](https://multimedia.cx/mirror/cinepak.txt),
located through [MultimediaWiki's Cinepak page](https://wiki.multimedia.cx/index.php/Cinepak).
Neither retrieved page specified a license for implementation code; none was
incorporated. The description supplied structural facts only. All decoder code
and synthetic fixtures are independently written for Reluva under GPL-3.0-or-later.
The wiki was a route to the primary description, not a source of decoder code.

The described stream has big-endian frame/strip/chunk fields within AVI's
little-endian container. Frame flags control strip codebook inheritance. Separate
V1/V4 codebooks contain at most 256 four-luma entries with optional signed U/V.
V1 expands one entry over a 4x4 block; V4 uses four entries. Full and selective
codebook chunks update persistent state. Vector chunks select V1/V4 or preserve
previous pixels. Flag bits are consumed most significant first and can cross
32-bit word boundaries. The implementation uses the specified color matrix;
integer U/2 rounding was verified against independent decoded output.

Original Boggle media supplied additional evidence: header dimensions, rates,
strip coordinates, interleaved PCM and word-aligned vector chunks with an
arbitrary unused alignment byte. The source identities are in the
[Boggle manifests](../plans/boggle-1997/inspection.md). No retail packet, codebook,
sample, frame or decoded screenshot is included in the repository.

## Supported boundary

The container walker validates RIFF/LIST/chunk extents, padding, stream counts,
headers, timing fields and declared video frame counts. Nested `rec ` lists are
supported. Other video/audio stream arrangements, codecs and media chunks are
rejected. OpenDML/AVIX, Sega-specific variants and palette AVI are not supported.

The Cinepak path accepts 24-bit `cvid`/`CVID` AVI, color or grayscale codebooks,
full-width vertically stacked strips, absolute coordinates or zero-Y relative
strip heights, intra/inter strips, and full/selective codebooks. Unsupported
chunk IDs, invalid references, missing entries, truncation, incorrect geometry,
first-frame skip blocks and incomplete block coverage fail explicitly. Previously
reconstructed pixels and per-strip codebooks persist only within one read.
Failures clear all output, even after valid frames have already been decoded.

PCM format 1 permits mono/stereo, unsigned 8-bit or signed little-endian 16-bit,
up to 192 kHz. Packet payloads are concatenated before sample alignment checks.
Packet contents determine audio length: the supplied `INTRO.AVI` audio header advertises
three more samples than its actual payload, so that field is not used to invent
samples. Audio initial-interleave fields do not introduce an artificial delay.

Limits: 64 MiB input, 512 video frames, dimensions up to 2048 (multiples of four),
32 strips, nesting depth eight, 16 MiB source PCM and a conservative 128 MiB
budget for RGBA frames plus audio. Codebook storage is bounded and heap allocated.
Boggle imposes smaller button geometry/frame limits before using menu clips.
`durationMillis()` and `frameAt()` implement bounded rational timing; looping
reduces elapsed time before multiplication, including very large clock values.

## Verification

Synthetic tests cover V1/V4 placement, positive/negative chroma rounding and
clamping, PCM 8/16-bit conversion, codebook updates, retained blocks, selector
bits crossing flag words, multi-strip inheritance versus independent books,
relative/absolute strip coordinates, alignment bytes, truncated/invalid input,
undefined entries, unsupported chunks and allocation limits.

An external comparison used Debian's FFmpeg executable
`7.1.5-0+deb13u1` (its `-L` reports GPL-2.0-or-later), solely as a reference tool.
No FFmpeg implementation source was consulted or copied and it is not a runtime
dependency. All 704 frames from seven supplied clips matched every RGBA byte:
`INTRO.AVI`, the five mode-button clips, and the two-strip `Hasbro.avi`. Decoded
PCM from both audio-bearing clips also matched every signed 16-bit sample.
Reference output and detailed comparison logs remain outside every Git worktree.
