# Star Trek: Birth of the Federation: the main resource archive (`stbof.res`)

> **Status:** decoded · **Code:** `src/common/zip.cpp` · **Checked on:** the English and German retail `stbof.res` (6,013 and 6,014 entries), every entry listed (October 2026) · **Confidence:** confirmed unless marked

## Overview

`stbof.res` holds most of the game's data: interface graphics, window
layouts, models and their textures, fonts, text and the data tables. It is
an ordinary zip archive that Python's `zipfile` reads without complaint, so
Reluva reads it with the common zip reader as it is. The files inside are
mostly not looked at yet; those that are have their own pages, linked
below.

## Layout

A plain zip. 6,013 entries (the German one 6,014), no folders, names in
lower case. 299 entries are stored (method 0: all the `.gif` files) and the
rest deflated (method 8).

### Contents, by extension

| Ext | Files | Total size | Examples | What they are | Confidence |
|---|---|---|---|---|---|
| `.tga` | 4,864 | 215 MB | `asave.tga`, `asave_on.tga`, `oaccept1.tga` | interface graphics, with their text drawn in | confirmed |
| `.wdf` | 377 | 468 KB | `options.wdf`, `tactical.wdf`, `gmesetup.wdf` | window layouts, one per screen or dialog ([wdf.md](wdf.md)) | confirmed |
| `.gif` | 299 | 20 MB | `t01_a.gif`, `bh01.gif` | textures of the `.hob` models (same base names) | inferred |
| `.hob` | 265 | 5 MB | `t01_a.hob`, `ch1_a.hob`, `cphaser.hob` | 3D models for ship combat; `_a`/`_b`/`_c` look like levels of detail | guessed |
| `.ani` | 65 | 13 MB | `gmel.ani`, `bnol.ani`/`bnom.ani`/`bnos.ani` | animations; `l`/`m`/`s` endings in three sizes (76,860 / 27,708 / 12,348 bytes) | guessed |
| `.cur` | 26 | 544 KB | `generic.cur`, `shipmove.cur`, `hwait.cur` | mouse cursors, some per race | guessed |
| `.tdb` | 22 | 339 KB | `cdiplomt.tdb`, `minor1.tdb`, `teachtip.tdb`, `treaty.tdb` | text: diplomacy per race, minor races, tutorial tips, intelligence reports ([tdb.md](tdb.md)) | confirmed |
| `.fnt` | 21 | 962 KB | `human15.fnt`, `klingn28.fnt`, `generic9.fnt` | fonts per race in 9, 15, 20 and 28 point ([fnt.md](fnt.md)) | confirmed |
| `.scr` | 20 | 3 KB | `ceogalli.scr` | end-of-game scripts, matching the CD's `*EOG*.wav` | guessed |
| `.bin` | 18 | 159 KB | `morale.bin`, `planboni.bin`, `shipname.bin`, `starname.bin`, `combat.bin`, `converse.bin` | rule and name tables ([tables.md](tables.md)) | confirmed |
| `.wtf` | 7 | 7 KB | `aiempire.wtf`, `aiminor.wtf`, `ccolval.wtf` | AI weights; colonisation values per race ([tables.md](tables.md)) | confirmed |
| `.tec` | 4 | 59 KB | `techtree.tec`, `techdesc.tec`, `tecfield.tec`, `racetech.tec` | technology tree and text ([tables.md](tables.md)) | confirmed |
| `.sst` | 4 | 70 KB | `shiplist.sst`, `shiptech.sst`, `shiprace.sst`, `shipdesc.sst` | ship classes ([tables.md](tables.md)) | confirmed |
| `.bst` | 3 | 127 KB | `edifice.bst`, `edifdesc.bst`, `edifbnft.bst` | buildings ([tables.md](tables.md)) | confirmed |
| `.rst` | 2 | 16 KB | `race.rst`, `racedesc.rst` | races ([tables.md](tables.md)) | confirmed |
| `.pst` | 1 | 85 KB | `planet.pst` | planets ([tables.md](tables.md)) | confirmed |
| `.est` | 1 | 780 B | `environ.est` | planet environments ([tables.md](tables.md)) | confirmed |
| `.lst` | 4 | 146 KB | `palette.lst`, `color.lst`, `texture.lst`, `keywords.lst` | not looked at | |
| `.mnu` | 3 | 3 KB | `combat.mnu`, `fleets.mnu`, `system.mnu` | menus ([mnu.md](mnu.md)) | confirmed |
| `.vtx` | 2 | 10 KB | `laser.vtx`, `rlit.vtx` | combat effects | guessed |
| `.dic` | 1 | 82 KB | `lexicon.dic` | text ([lexicon.md](lexicon.md)) | confirmed |
| `.smt`, `.tfd` | 1 each | | `objstruc.smt`, `tfdesign.tfd` | not looked at | |
| `.tbl` | 1 | 7 KB | `music.tbl` | binary, 7,323 bytes; starts with `u32` values all 0 or 1. Probably which of `music.snd`'s pieces may play in which situation ([snd.md](snd.md)) | guessed |
| `.txt` | 1 | 8 KB | `credits.txt` | | |

### Races

Per-race files use one letter: `c`, `f`, `h`, `k`, `r`. The fonts are named
`cardsn`, `ferngi`, `human`, `klingn` and `romuln`, so `h` is the
Federation (human) and `f` the Ferengi: the Hall of Fame's rows, framed
with `hh_fm1.tga` and `hf_fm1.tga`, bear it out against a screenshot. The CD's movies
(`C.avi`, `F.avi`, `H.avi`, `K.avi`, `R.avi`) and end-of-game files use the
same letters.

### English and German

The German archive has one more file (`rb_sbar.tga`), and 1,986 files
differ: 1,865 `.tga` (their text is part of the picture), 79 `.wdf`, all 22
`.tdb`, and the text-bearing tables (`.tec`, `.bst`, `.rst`, `.sst`,
`.pst`, `.est`, `.mnu`, `lexicon.dic`, `credits.txt`, `meplanet.bin` and
`shipname.bin`). Everything else, models and textures included, is shared.

## In Reluva

Read with the common zip reader (`src/common/zip.cpp`), found by
`src/engines/botf/game_files.cpp`. reluva-viewer's Birth of the Federation
pages show what's inside: the TGAs on the Images page, and the fonts, text,
data tables and window layouts on their own pages (see each format's page).

## Sources and checks

- **Where the facts come from:** the zip listings of the English and German
  `stbof.res` (`tools/botf-re/botf_sample.py`), October 2026.
- **How it was checked:** every entry of both archives listed and compared
  by name, size and contents. The guesses at what each extension holds are
  from the names and sizes; the ones marked confirmed have been read whole
  (see their pages).

## Open questions

- What the `.hob`, `.ani`, `.cur`, `.scr`, `.vtx`, `.lst`, `.smt`, `.tfd`,
  `.tbl` and `credits.txt` files hold, past the guesses above.
