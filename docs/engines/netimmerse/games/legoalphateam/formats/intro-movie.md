# LEGO Alpha Team: the intro movie (`lmiintro.avi`)

> **Status:** decoded · **Code:** `src/common/video/movie.h` · **Checked on:** `lmiintro.avi`, the one file

## Overview

An AVI file ([avi.md](../../../../../common/video/avi.md)) with Indeo 5 video,
both read by Reluva's shared decoders.

## Layout

`lmiintro.avi` (39,840,492 bytes, CD only) is 640 × 480 Indeo 5 (`IV50`,
[indeo5.md](../../../../../common/video/indeo5.md)) at 15 frames a second, 1,632 frames (108.8 s), with
16-bit stereo PCM at 22,050 Hz, interleaved a chunk of each per frame. Its
frames are 115 key frames, 758 inter frames, 721 droppable inter frames and
38 null frames.

## In Reluva

The game's own program doesn't name the file: it was the CD's intro, not
the game's. Reluva plays it before the game if it has been copied beside
the install (`common/video/movie.h`); a key or click skips it.

## Sources and checks

The file is read by Reluva's shared decoders, whose pages
([avi.md](../../../../../common/video/avi.md),
[indeo5.md](../../../../../common/video/indeo5.md)) say where their facts
come from. How the Indeo 5 decoder was checked against this file is in
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) (the intro movie,
under the data formats).
