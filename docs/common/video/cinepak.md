# Cinepak video

## What it is

Cinepak (`cvid`) was the common video codec of mid-1990s CD-ROM games, in
Video for Windows (`.avi`) and QuickTime files. Reluva decodes it in
`src/video/cinepak.cpp` (`reluva::cinepak::Decoder`); the AVI files
around it are read by [avi.md](avi.md)'s reader.

## What Reluva supports

- **Cinepak pictures:** horizontal strips; each 4x4 block is one 2x2 YUV
  cell stretched over it (V1) or four cells (V4), from codebooks of 256
  each. Full and partial codebooks, colour and greyscale; intra, inter
  (skipped blocks keep the last picture) and all-V1 blocks. Codebooks carry
  over between frames, and from one strip to the next unless the frame's
  flag says otherwise. A strip whose top is 0 goes under the last. Colours:
  R = Y + 2V, G = Y - U/2 - V, B = Y + 2U. Output: RGBA32.

## Sources and checks

- **Where the facts come from:** the format as public write-ups and the
  open-source decoders document it (the Cinepak description on
  multimedia.cx, FFmpeg's `libavcodec/cinepak.c`), read for facts only; the
  code is new. It came to Reluva
  with the Star Trek: Generations engine (STG-Engine).
- **How it was checked:** `common.unit.video` decodes frames built in code
  (V1 and V4 blocks, an inter frame skipping a block, a dropped frame) and an
  AVI holding them with sound. On Generations' own `MOV_07.AVI` (640x320,
  665 frames), every pixel of every frame matched FFmpeg's decoding.
- **Clue's videos:** all 44 of Clue's AVIs (the same files on the CD, in
  its install and in the 2.2 patch's; the US and UK games share them)
  decode to the same pictures
  as the decoder the Clue engine had before it moved to this one (written
  from the MultimediaWiki description alone), but for green, which differs
  by 1 in 255 where U is odd and negative (U/2 rounds towards 0 here, down
  there). Their sound, 16-bit mono PCM at 22,050 Hz, reads the same. Clue's
  clip of Mrs. Peacock with the candlestick (`MP_candle.avi`) was checked
  against a 640x480 recording of the original game: a mean difference of 4
  in 255 per colour, the recording's compression's blur. The files
  themselves are described in [the Clue engine's docs](../../engines/clue/formats/videos.md).
- **Confidence:** published unless marked.

## Used by

- **`stgenerations`**: Star Trek: Generations' movies.
- **`clue`**: Clue's intro, deal, suggestion and prison clips
  ([videos.md](../../engines/clue/formats/videos.md)).
