# Need for Speed II SE: movies (`.dct`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/dct.cpp` · **Checked on:** all 15 movies on the US CD · **Confidence:** confirmed unless marked

## Overview

The game's movies, on the CD in `FeData/pc/movies/`: the title movie
(`titleav.dct`), three attract-mode movies (`demoav0.dct` to
`demoav2.dct`) and one for each of 11 cars (`f355av.dct`, `mustav.dct`
and others). All are 304x224 at 15 frames a second, with stereo sound.

The same folder's `demo.0`, `demo.1` and `demo.2` aren't movies: the
three are the same 124,344 bytes, mostly zeros. Neither program names
them, and the attract mode plays `demoav0.dct`-`demoav2.dct`: they seem
unused *(inferred)*.

## Layout

An EA sound stream as the game's music is
([mus.md](../../../formats/mus.md)): `SCHl`, `SCCl`, `SCDl` blocks of
EA-XA sound (22,050 Hz, stereo), `SCEl`. Between its chunks are the
pictures, one to a chunk:

| Offset | Size | What |
|---|---|---|
| 0 | 4 | `mTCD` |
| 4 | u32 | The chunk's size, these 16 bytes included |
| 8 | u32 | 0 in every chunk |
| 12 | u16 | The picture's width |
| 14 | u16 | Its height |
| 16 | | One MDEC frame ([mdec.md](../../../../../common/video/mdec.md)) |

Every frame stands alone. The first comes after `SCHl`, the second after
`SCCl`, then one after each sound block but the last few. There's no
frame rate in the file: frames × the sound's rate ÷ its samples gives
15.00 for every movie, so the sound and the pictures end together.

## In Reluva

Read as `DctMovie` (`src/engines/nfs/nfs2se/formats/dct.cpp`), its sound
by the music's reader, which passes over the `mTCD` chunks.
`reluva-tool nfs survey` counts the movies, frames and soundtracks;
`reluva-tool nfs dct-frames` writes frames as pictures.

The data viewer's Movies page (`src/engines/nfs/nfs2se/pages/movie_page.cpp`)
plays them with their sound, which keeps the time, as NFS III's page
plays its `.mad` movies.

## Sources and checks

- **Where the facts come from:** the files. The chunk's tag and size were
  plain; the frames were recognised by MDEC's `0x3800` and worked out
  with a prototype decoder in Python until the pictures came out whole;
  `ffprobe` reads the stream as MDEC too.
- **How it was checked:** `reluva-tool nfs survey` on the CD (November
  2026): 15 movies, 9,340 frames and 15 soundtracks read, none failing;
  each movie's frames compared with FFmpeg's decoding
  ([mdec.md](../../../../../common/video/mdec.md)); the Movies page played;
  the unit tests (`nfs.unit.nfs2se`).

## Open questions

- The u32 at 8, 0 in every chunk.
- When the game plays the attract-mode movies (left at the main menu,
  seen on the host), and whether anything reads `demo.0`-`demo.2`.
