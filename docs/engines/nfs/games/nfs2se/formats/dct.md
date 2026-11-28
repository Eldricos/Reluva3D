# Need for Speed II SE: movies (`.dct`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/dct.cpp` · **Checked on:** all 15 movies on the US CD · **Confidence:** confirmed unless marked

## Overview

The game's movies, on the CD in `FeData/pc/movies/`: the title movie
(`titleav.dct`), three attract-mode movies (`demoav0.dct` to
`demoav2.dct`) and one for each of 11 cars (`f355av.dct`, `mustav.dct`
and others). All are 304x224 at 15 frames a second, with stereo sound.

The same folder's `demo.0`, `demo.1` and `demo.2` aren't movies: the
three are the same 124,344 bytes, a recorded race in the layout of the
game's replays (its settings, "Player1" at 0xA8, the controls frame by
frame from 0x5B4), 4 bytes longer than what the program writes
(`replay.rpy`, 0x45b440), so from an earlier build *(inferred)*.
Neither program opens them: the only "demo" either has is `demoav`, with `%s%d.dct`
(0x40d972), the attract mode playing `demoav0.dct` to `demoav2.dct` in
turn after 25 s at the main menu without a key: more than 3,200 ticks
of its clock (0x40d951), which runs at 128 a second (0x47212c); the
3dfx program the same (0x40d726). Recorded races were the attract mode,
perhaps, before the movies *(guessed)*.

## Layout

An EA sound stream as the game's music is
([mus.md](../../../formats/mus.md)): `SCHl`, `SCCl`, `SCDl` blocks of
EA-XA sound (22,050 Hz, stereo), `SCEl`. Between its chunks are the
pictures, one to a chunk:

| Offset | Size | What |
|---|---|---|
| 0 | 4 | `mTCD` |
| 4 | u32 | The chunk's size, these 16 bytes included |
| 8 | u32 | 0 in every chunk; nothing reads it |
| 12 | u16 | The picture's width |
| 14 | u16 | Its height |
| 16 | | One MDEC frame ([mdec.md](../../../../../common/video/mdec.md)) |

The players of both programs (`nfs2sen.exe` 0x422e60; `NFS2SEA.EXE`'s
the same) keep the chunks tagged `mTCD`, take the size at 4 and the
picture's size at 12 and 14 (0x422f1e), and decode from 16 (0x423270).
Nothing reads the word at 8: the hosted game played a copy of
`titleav.dct` with it set to other values in all 1,761 chunks, and every
frame came out the same.

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
