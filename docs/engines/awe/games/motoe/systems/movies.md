# Murder on the Orient Express: movies and video loops

> **Code:** `src/engines/awe/movie_player.cpp` · **Confidence:** from the code unless marked

## Overview

The game's 46 movies (`Movies/`) and its video loops (`Elements/` and the
scenes' cutouts, 39 of them with an alpha plane) are Bink movies, decoded
by Reluva's shared decoder ([bink.md](../../../../../common/video/bink.md)),
with `binkw32.dll`'s own colour conversion.

## How it works

- **Movies** (`movie <name>`) play full screen with their sound decoded
  whole ahead, their subtitles under them
  ([script.md](script.md)).
- **Video loops**, objects whose file is a `.bik`, play in their scene as
  pictures do, from the start again when they've gone round
  (`Movie::rewind`), or once and held on the last frame with `BNKX loop
  0` ([ac2.md](../formats/ac2.md)).

## Sources and checks

- `reluva-tool awe files-check <game folder> -bik` decodes every movie and
  loop of an install, and `awe bink-info` shows a file's header and frames
  (`-yuv`, `-wav`, `-tga` to decode it).
