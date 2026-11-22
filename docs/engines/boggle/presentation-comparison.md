# Classic presentation comparisons

`reluva-tool boggle frame-compare <reference> <native> [x y width height]`
compares two decoded images with matching dimensions. An optional rectangle
limits comparison to a board, dialog or word panel. It reports RGB pixels,
changed pixels, maximum channel delta, mean absolute channel error and RMS
channel error. Alpha is ignored. It exports no pixels and makes no automatic
rescaling/alignment assumptions. Encoded inputs are capped at 32 MiB, decoded
canvases at 4096x4096, and invalid regions/dimensions fail explicitly.

Keep original/native screenshots and all derived output outside Git. Match the
board, orientation, game phase, viewport and settings before comparing. The
original and native default canvases differ; any external crop/rescale must be
recorded rather than interpreted as direct original pixel parity. Low error in
a menu region does not establish gameplay, clocks or AI parity. Synthetic tests
check independent known deltas, region isolation, alpha handling and invalid data.

## Decoder and nominal media timing comparison

An external harness decodes the owner's installed files with the native readers
and separately with FFmpeg/ffprobe. Every RGB pixel in all 814 frames matched:

| Clip | Frames |
| --- | ---: |
| Hasbro | 156 |
| INTRO | 377 |
| ClsBut8 / BrkBut8 / BatBut8 / TetBut8 | 30 each |
| SpcBut8 | 51 |
| ClsO1All / ClsM1All | 55 each |

This covers 67,840,800 RGB pixels, with maximum delta, mean error and RMS error
all zero. Both intro PCM streams also match byte-for-byte: 229,320 and 554,190
signed 16-bit samples. Dimensions, frame counts and rational frame rates agree
with ffprobe. Each Cinepak frame's earliest millisecond boundary was checked
against `frameAt`; no fixed-15-fps rounding was substituted for INTRO's
1000000/66667 rational rate. Raw frames, PCM, scripts and full logs remain in
the external analysis directory; only numerical findings are committed.

This establishes decoding and nominal timing, not original application startup
latency, sound-device delay, dropped frames or full-screen pixel matching.
Original mode-button hover/sound triggers, idle computer wandering and exact
lighting/rasterization still require further original-program comparisons. The
completed AI selection/cadence milestone remains valid independently of these
presentation details; no timing or idle motion is invented to fill the gaps.

## Current validation

The normal suite passes 49 checks and skips 28 optional data checks. All 42
Boggle checks applicable to the restored English Classic set pass; full
multilingual resource/dictionary surveys are not run against this partial
restoration. Frontend, round, user-dictionary and options suites pass ASan/UBSan
with LeakSanitizer disabled for the execution tracer. External SDL runs inspect
both variants' roster backgrounds/titles, editor and high-score background,
enter rounds and restore text input. Existing gameplay snapshots and fresh-copy
Classic/AI/audio metadata remain unchanged. Windows validation remains pending.
