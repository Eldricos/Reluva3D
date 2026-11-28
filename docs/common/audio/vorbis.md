# Ogg Vorbis

## What it is

Ogg Vorbis sound files, decoded whole into a `Sound`:
`reluva::audio::decodeVorbis` (`src/audio/vorbis.cpp`), and
`decodeSound` for any file starting `OggS`. The decoding is Xiph's
reference libvorbis 1.3.7 with libogg 1.3.6 (BSD licence), downloaded
and built by CMake (the top `CMakeLists.txt`); Reluva's part reads the
file from memory through vorbisfile's callbacks.

## What Reluva supports

- Vorbis I in Ogg, mono or stereo, at any rate.
- Files from before Vorbis 1.0, which use floor 0 (as the encoders of
  2000-2001 did). Not every decoder takes these: stb_vorbis, for one,
  refuses them.
- Chained files, as long as every link has the same rate and channels.
- Not: more than two channels; streaming as it plays (a file is decoded
  whole).

## Sources and checks

- **Where the facts come from:** the decoding is libvorbis's own.
- **How it was checked:** a unit test (`common.unit.audio`, its `vorbis`
  case) on a 0.2 s tone made with FFmpeg's libvorbis encoder
  (`tests/common/data/tone440_ogg.h`): its rate, channels, exact length
  and pitch. And 27 of Star Trek: Away Team's sounds, made with libVorbis
  of October 2000 and January 2001, compared with FFmpeg's decoder: the
  same rate and channels, the samples' total level the same within one
  part in a million, and the same length but for 5 files, where
  libvorbis gives 128 samples more at the end. The game's own decoder
  (libVorbis I of February 2001) gives libvorbis's lengths
  ([file survey](../../engines/stawayteam/research/file-survey.md#sound)).
- **Confidence:** published unless marked
  (docs/DOCUMENTATION_STANDARD.md).

## Used by

- **`stawayteam`**: every sound, voice and song in Star Trek: Away Team
  ([research/file-survey.md](../../engines/stawayteam/research/file-survey.md#sound)).
