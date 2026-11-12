# AVI files

## What it is

`src/common/video/avi.{h,cpp}` (`reluva::avi::Decoder`) reads Microsoft's AVI container
far enough to play a movie: the first video stream's frames and the first
audio stream's PCM. For another codec than Cinepak it hands out each
frame's bytes (`readFrame`), which is how the Indeo 5 movie player
(`common/video/movie.h`) uses it. The layout is Microsoft's published
RIFF/AVI format.

## What Reluva supports

- **AVI files:** the first video stream (its codec named by `videoCodec()`;
  frames decode when it's Cinepak, `readFrame()` hands over any codec's
  frames as stored) and the first audio stream when it's PCM, read whole
  (`audio()`). Frames inside `rec ` lists are found; `nndc`, `nndb` and
  `nnid` chunks are video (Generations names its inter frames `00id`),
  `nnwb` sound. The frame time is the main header's.
- **Not handled:** the index (`idx1`; frames are found by walking the
  file), OpenDML (files over 1 GB), compressed sound, and palette changes
  for 8-bit codecs.

## The format

- The file is `RIFF` chunks: a four-character id, a 32-bit little-endian
  size, the data, and a pad byte if the size is odd. A `LIST` chunk's data
  is a four-character type and more chunks.
- `RIFF` of type `AVI ` holds a `LIST hdrl`, with a `LIST strl` per stream,
  and a `LIST movi` with the data. `JUNK` chunks are padding; an `idx1`
  index may follow `movi`, and isn't needed.
- `avih`, first in `hdrl`, starts with the microseconds a frame
  (`lmiintro.avi`: 66,667, 15 a second), which the decoder times frames by.
- In each `strl`:
  - `strh` gives the stream's type (`vids`, `auds`) and handler (the video
    codec's fourcc, `IV50`) at 0 and 4, then scale and rate at 20 and 24:
    rate ÷ scale is the frames a second (agreeing with `avih`'s);
  - `strf`: for video a `BITMAPINFOHEADER` (width at 4, height at 8); for
    audio a `WAVEFORMATEX` (format tag, channels, samples a second, bytes a
    second, block align, bits a sample).
- In `movi`, each chunk's id is the stream's number in two decimal digits
  and a type: `dc` or `db` a video frame, `wb` audio. Chunks may be grouped
  in `LIST rec ` lists. A video chunk of size 0 is a dropped frame: the
  picture stays.

## Sources and checks

- **Where the facts come from:** Microsoft's published RIFF/AVI format.
- **How it was checked:** what it reads of LEGO Alpha Team's
  `lmiintro.avi` was checked against what FFmpeg's `ffprobe` reports for
  it, and Star Trek: Generations' and Clue's AVIs decode as
  [cinepak.md](cinepak.md) says.
- **Tools:** `reluva-tool avi-frames <file.avi> [folder] [frames... | all]`
  lists an AVI's streams, decodes every frame, timed, and writes those
  asked for as TGA files.
- **Confidence:** published unless marked.

## Used by

- **`stgenerations`** and **`clue`**: their movies, in Cinepak.
- **`netimmerse`**: LEGO Alpha Team's intro movie, in Indeo 5
  ([intro-movie.md](../../engines/netimmerse/games/legoalphateam/formats/intro-movie.md)).
