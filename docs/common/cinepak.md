# Cinepak videos in AVI files (.avi)

Cinepak (`cvid`) was the common video codec of mid-1990s CD-ROM games, in
Video for Windows (`.avi`) and QuickTime files. Star Trek: Generations
(`stgenerations`) plays its movies from AVIs with Cinepak video. Reluva
decodes Cinepak in `src/common/video/cinepak.cpp`
(`reluva::cinepak::Decoder`) and reads AVIs in `src/common/video/avi.cpp`
(`reluva::avi::Decoder`).

## What it decodes

- **Cinepak pictures:** horizontal strips; each 4x4 block is one 2x2 YUV
  cell stretched over it (V1) or four cells (V4), from codebooks of 256
  each. Full and partial codebooks, colour and greyscale; intra, inter
  (skipped blocks keep the last picture) and all-V1 blocks. Codebooks carry
  over between frames, and from one strip to the next unless the frame's
  flag says otherwise. A strip whose top is 0 goes under the last. Colours:
  R = Y + 2V, G = Y - U/2 - V, B = Y + 2U. Output: RGBA32.
- **AVI files:** the first video stream (its codec named by `videoCodec()`;
  frames decode when it's Cinepak, `readFrame()` hands over any codec's
  frames as stored) and the first audio stream when it's PCM, read whole
  (`audio()`). Frames inside `rec ` lists are found; `nndc`, `nndb` and
  `nnid` chunks are video (Generations names its inter frames `00id`),
  `nnwb` sound. The frame time is the main header's.
- **Not handled:** the index (`idx1`; frames are found by walking the
  file), OpenDML (files over 1 GB), compressed sound, and palette changes
  for 8-bit codecs.

## Sources and checks

- **Where the facts come from:** the format as public write-ups and the
  open-source decoders document it (the Cinepak description on
  multimedia.cx, FFmpeg's `libavcodec/cinepak.c`), read for facts only; the
  code is new. Microsoft's documentation of RIFF and AVI. It came to Reluva
  with the Star Trek: Generations engine (STG-Engine).
- **How it was checked:** `common.unit.video` decodes frames built in code
  (V1 and V4 blocks, an inter frame skipping a block, a dropped frame) and an
  AVI holding them with sound. On Generations' own `MOV_07.AVI` (640x320,
  665 frames), every pixel of every frame matched FFmpeg's decoding.
