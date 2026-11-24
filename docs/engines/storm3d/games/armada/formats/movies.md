# Star Trek: Armada: movies (`.bik`)

> **Status:** decoded · **Code:** `src/engines/storm3d/pages/movie_page.cpp` (the viewer), `src/video/bink.cpp` (reading) · **Checked on:** the English retail 1.0 install's 68 movies and the CD's opening movie · **Confidence:** confirmed unless marked

## Overview

The movies are Bink files, RAD Game Tools' format, played with the
`binkw32.dll` in the install's folder:

| Where | Files | What |
|---|---|---|
| `animations/` | 68 | The briefings' faces (`Worf.bik`, `Toral.bik`), the missions' pictures (`Borg01.bik`, `r4pou1.bik`...), the front end's (`Single01.bik`, `Multi01.bik`, `Credit01.bik`) |
| The CD's `setup/animations/STIntro.bik` | 1 | The opening movie, 4 minutes 20 seconds |

The installer leaves the opening movie on the CD. The executable looks
for it as `animations\stintro.bik`, and on the CD's drive at
`movie\stintro.bik` and `setup\animations\stintro.bik`. It also names an
`addon\` folder of the front end's movies (`addon\single01.bik`), which
the install doesn't have *(inferred)*: perhaps for an expansion.

## Layout

All 69 are Bink revision `f`, whose layout is on the shared page
([bink.md](../../../../../common/video/bink.md)). Their sizes:

| Size | Movies |
|---|---|
| 400×200 | 38 |
| 320×240 | 12, with the opening movie |
| 280×190 | 8 |
| others, from 80×220 to 370×160 | 11 |

They run at 15 frames a second (53 of the install's) or 30 (15). 39 of
the install's have sound, all mono: 38 at 22,050 Hz and one at 44,100
Hz. The opening movie's is 44,100 Hz stereo.

## In Reluva

Movies are read with the shared Bink decoder (`bink::Movie`,
`src/video/bink.h`), their colours converted as the game shows them, the
chroma's parts exchanged (`Rgb::kExchanged`,
[systems/front-end.md](../systems/front-end.md#the-movies-colours)).

- **The data viewer's Movies page** (`MoviePage`,
  `src/engines/storm3d/pages/movie_page.h`) plays each in the install,
  its sound beside it.
- `reluva-tool storm3d movie-check <game folder> [name filter]` decodes
  every frame and the sound of each movie.
- `reluva-tool storm3d movie-frame <game folder> <name> <frame> <out.png>`
  writes one frame as PNG.

## Sources and checks

- **Where the facts come from:** the movies' headers; the executable's
  strings for where it looks for them.
- **How it was checked:** `movie-check` decodes every frame and the sound
  of all 69; for each movie with sound, the sound is as long as the
  pictures to within one frame (0.033 s at most). Frames of the opening
  movie (frame 1,500 of 3,895), a briefing's face and a mission's picture
  were looked at as PNGs and are whole and in their colours.

## Open questions

- Why the game's colours have the chroma's parts exchanged
  ([systems/front-end.md](../systems/front-end.md#the-movies-colours)),
  and whether every pixel matches Armada's own `binkw32.dll`. The awe
  engine's checker (`reluva-tool awe bink-check`), which compares the two
  on Murder on the Orient Express's revision `i` movies, can't run this
  older DLL yet: it starts a thread as it opens a movie
  (`CreateThread`), which the checker's stand-in Windows doesn't have.
