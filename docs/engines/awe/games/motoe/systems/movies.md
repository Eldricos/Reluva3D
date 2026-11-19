# Murder on the Orient Express: movies and video loops

> **Status:** done · **Code:** `src/engines/awe/render/movie_player.cpp` · **Checked against:** the game's `binkw32.dll` (English 1.0), run in Reluva's x86 interpreter, on all 87 movies and loops · **Confidence:** from the code unless marked

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
- `reluva-tool awe bink-check <game folder> <movie.bik>...` decodes a file
  by Reluva and by the game's `binkw32.dll` in the x86 interpreter, its
  frames copied out as the game copies them, and compares them pixel by
  pixel. Over all 87 of the install's files, every frame tried is the
  same, apart from pixels over Y 235, where the DLL's path without MMX
  clamps ([bink.md](../../../../../common/video/bink.md)).
