# AVI files

`src/common/video/avi.{h,cpp}` reads Microsoft's AVI container far enough
to play a movie: the first video stream's frames and the first audio
stream's PCM. The layout is Microsoft's published RIFF/AVI format; the
reader was checked on LEGO Alpha Team's `lmiintro.avi` against what FFmpeg's
`ffprobe` reports for it.

- The file is `RIFF` chunks: a four-character id, a 32-bit little-endian
  size, the data, and a pad byte if the size is odd. A `LIST` chunk's data
  is a four-character type and more chunks.
- `RIFF` of type `AVI ` holds a `LIST hdrl`, with a `LIST strl` per stream,
  and a `LIST movi` with the data. `JUNK` chunks are padding; an `idx1`
  index may follow `movi`, and isn't needed.
- In each `strl`:
  - `strh` gives the stream's type (`vids`, `auds`) and handler (the video
    codec's fourcc, `IV50`) at 0 and 4, then scale and rate at 20 and 24:
    rate ÷ scale is the frames a second;
  - `strf`: for video a `BITMAPINFOHEADER` (width at 4, height at 8); for
    audio a `WAVEFORMATEX` (format tag, channels, samples a second, bytes a
    second, block align, bits a sample).
- In `movi`, each chunk's id is the stream's number in two decimal digits
  and a type: `dc` or `db` a video frame, `wb` audio. Chunks may be grouped
  in `LIST rec ` lists. A video chunk of size 0 is a dropped frame: the
  picture stays.

## LEGO Alpha Team's intro

`lmiintro.avi` (39,840,492 bytes, CD only) is 640 × 480 Indeo 5 (`IV50`,
[indeo5.md](indeo5.md)) at 15 frames a second, 1,632 frames (108.8 s), with
16-bit stereo PCM at 22,050 Hz, interleaved a chunk of each per frame. Its
frames are 115 key frames, 758 inter frames, 721 droppable inter frames and
38 null frames.

The game's own program doesn't name the file: it was the CD's intro, not
the game's. Reluva plays it before the game if it has been copied beside
the install (`common/video/movie.h`); a key or click skips it.
