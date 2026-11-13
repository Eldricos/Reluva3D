# Smacker videos (`.smk`)

## What it is

RAD Game Tools' video format from before Bink, used by many 1990s games.
Reluva decodes SMK2 and SMK4 in `src/video/smacker.cpp`
(`reluva::smacker::Decoder`).

## What Reluva supports

- **Pictures:** 8-bit, with a palette that can change from frame to frame
  (set, keep and copy commands). The picture is coded in 4x4 blocks (mono,
  full, skip and fill), with four Huffman trees given in the file's header,
  and their escape ("recent value") leaves.
- **Doubled and interlaced videos** are shown at twice their stored height
  (`height()` is the height shown).
- **Sound:** the first audio track with data, raw or Huffman-packed DPCM,
  8 or 16 bits, mono or stereo. `takeAudio()` hands over what each frame
  carried, for a `QueuingStream` (`src/audio/stream.h`).
- **Not handled:** Bink audio inside a Smacker file (flag `0x08000000`), and
  the ring frame's looping (a player may restart from frame 1 itself).

## Sources and checks

- **Where the facts come from:** the format as the open-source decoders
  document it (FFmpeg's `libavcodec/smacker.c`, libsmacker, ScummVM's
  `video/smk`), read for facts only; the code is new. It came to Reluva
  with the Harbinger engine (STDS9H).
- **How it was checked:** `tests/common/data/smacker/make_smacker.py` writes
  small synthetic files that use every block type, the escape leaves, each
  palette command and both audio paths. FFmpeg, an independent decoder,
  made the expected frames and sound; `common.unit.video` compares every
  pixel and sample with them (`tests/common/data/smacker_videos.h`).
- **On a game's own files:** what has been checked against Harbinger's
  videos is in its [TESTING.md](../../engines/stharbinger/TESTING.md).
- **Confidence:** published unless marked.

## Used by

- **`stharbinger`**: Harbinger's intro, cut scenes and action sequences
  ([videos.md](../../engines/stharbinger/formats/videos.md)).
