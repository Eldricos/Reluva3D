# Cinepak videos in AVI files (.avi)

Cinepak (`cvid`) was the common video codec of mid-1990s CD-ROM games, in
Video for Windows (`.avi`) and QuickTime files. Star Trek: Generations
(`stgenerations`) plays its movies from AVIs with Cinepak video, and
Clue: Murder at Boddy Mansion (`clue`) its intro and suggestion clips. Reluva
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
- **Clue's videos:** all 132 AVIs of Clue's installs and CD (the 44 of an
  install, and the US and UK sets of the CD) decode to the same pictures
  as the decoder the Clue engine had before it moved to this one (written
  from the MultimediaWiki description alone), but for green, which differs
  by 1 in 255 where U is odd and negative (U/2 rounds towards 0 here, down
  there). Their sound, 16-bit mono PCM at 22,050 Hz, reads the same. Clue's
  clip of Mrs. Peacock with the candlestick (`MP_candle.avi`) was checked
  against a 640x480 recording of the original game: a mean difference of 4
  in 255 per colour, the recording's compression's blur. The files
  themselves are described in [the Clue engine's docs](../engines/clue/README.md#videos).
- **Tools:** `reluva-tool avi-frames <file.avi> [folder] [frames... | all]`
  lists an AVI's streams, decodes every frame, timed, and writes those
  asked for as TGA files.
