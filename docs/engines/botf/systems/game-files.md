# Star Trek: Birth of the Federation: the game's files

> **Status:** done · **Code:** `src/engines/botf/game_files.cpp`, `src/engines/botf/botf.cpp` · **Checked against:** directory listings of an English retail install and its CD · **Confidence:** confirmed unless marked

## Overview

Where the game's files are, what each is for, and how the game, and
Reluva, find them: in the install, in its language folder, and on the CD
for a "minimum" install.

## How it works

### The install and the CD

| File | Where | What |
|---|---|---|
| `stbof.res` | install; CD per language | zip archive of 6,013 files: pictures, models, layouts, data tables ([formats/stbof-res.md](../formats/stbof-res.md)) |
| `alt.res` | install, CD | 31 full-screen 800x600 16-bit TGAs, LZSS-compressed ([formats/alt-res.md](../formats/alt-res.md)) |
| `sfx.snd` | install, CD | sound effects: 163 WAV files ([formats/snd.md](../formats/snd.md)) |
| `english.snd` | install; CD `ENGLISH/` | speech: 1,364 IMA ADPCM clips ([formats/snd.md](../formats/snd.md)) |
| `music.snd` | CD only | music: 16 pieces in 151 stereo IMA ADPCM segments ([formats/snd.md](../formats/snd.md)) |
| `intro.wav`, `ENGLISH/INTRO.WAV` | install, CD | narration, a plain RIFF WAVE |
| `ENGLISH/*EOG*.wav` | CD | end-of-game narration per race and ending (`Alli`, `Anni`, `Domi`, `Loss`), with `*eog*.scr` in `stbof.res` |
| `ENGLISH/*.avi` | CD only | `open.avi` and one per race (`C`, `F`, `H`, `K`, `R`): Cinepak (`cvid`) video, 800x300; the race movies also have sound |
| `Stbof.ini` | install | settings, below |
| `*.sav` | install | saved games ([formats/sav.md](../formats/sav.md)) |
| `trek.exe`, `Trek.icd` | install | SafeDisc's loader and the encrypted game ([formats/executable.md](../formats/executable.md)) |

The install's copies are byte for byte the CD's (the same size and hash).

| File | Size | MD5 of first 5000 bytes |
|---|---|---|
| `stbof.res` (install; the CD's `ENGLISH/STBOF.RES` is the same) | 79,901,501 | `2e888c1eaefa901da9e2dbd6229798a5` |
| `GERMAN/STBOF.RES` (CD) | 79,385,299 | `21df64c3479690c70f50bfc0bed3792b` |
| `english.snd` (install and the CD's `ENGLISH/`) | 39,509,316 | `806bf768303d60745c792fae58ec506f` |
| `sfx.snd` (install and CD) | 7,747,778 | `ee602d2849a2731c789101ec5ea2e238` |
| `music.snd` (CD only) | 75,449,840 | `8c6b02cc653054fdcab78db463e5f06a` |
| `alt.res` (install and CD) | 12,992,536 | `85b83104c882c4e54c2084a1779e0d6e` |

### `Stbof.ini`

Plain `KEY=VALUE` lines, with no section:

```text
CDPATH=D:\
HDPATH=C:\botf\
CURRENTGAME=0
LANGUAGE=ENGLISH
MOVIEON=ON
SOUNDON=ON
MUSICVOLUME=100
FXVOLUME=100
VIDEOVOLUME=127
VOICEVOLUME=100
MONO=YES
STARTINGSEED=0
INSTALL=MINIMUM
DIRECT X NOT 6.0 INSTALLED BY BOTF
```

`INSTALL=MINIMUM` and `CDPATH` are why `music.snd` and the movies are
read from the CD. `LANGUAGE` names the CD's folder (`ENGLISH`, `GERMAN`),
and the speech archive (`english.snd`, `german.snd`).

Once the game has run, it keeps the options and the last game's setup
there too. The user's, after a game, adds:

```text
VICTORY=KILLALL
3D=SOFTWARE
TACTICAL=ABSTRACT
BORG=ON
AUTOSAVE=ON
TOOLTIPS=ON
RANDOMEVENTS=OFF
TURNSTATUS=OFF
TIMELIMIT=0
TACTIMELIMIT=0
SHAPE=1
SIZE=0
RACENUM=0
DIFFICULTY=1
CIVLEVEL1=0
...
CIVLEVEL5=0
ECONFILTER=55
EVENTFILTER=177
MILITARYFILTER=19
SCREENVOLATILE=NO
STARTINGSEED=1795138721
Version=1.0.2
```

| Keys | What they are |
|---|---|
| `VOICEVOLUME`, `VIDEOVOLUME`, `MUSICVOLUME`, `FXVOLUME` | the options screen's volumes, 0 to 127: 100, 127, 100, 100 in both files seen, as a fresh install's options screen shows them |
| `TOOLTIPS`, `AUTOSAVE`, `RANDOMEVENTS` | the options' settings, `ON` or `OFF` |
| `TACTICAL` | tactical combat: `ABSTRACT` automatic, `DETAILED` manual |
| `TIMELIMIT`, `TACTIMELIMIT` | the strategic and tactical timers, 0 none *(inferred)* |
| `SHAPE`, `SIZE`, `RACENUM`, `DIFFICULTY`, `CIVLEVEL1`-`CIVLEVEL5` | the last game's setup, each as a saved game's `gameInfo` holds it ([formats/sav.md](../formats/sav.md)): difficulty 1 is simple |
| `VICTORY` | the victory condition: `KILLALL` presumably domination *(guessed)* |
| `3D`, `BORG`, `TURNSTATUS`, `SCREENVOLATILE`, the three `FILTER`s | not known yet: 3D drawing in software, the Borg, the turn status, and the galaxy map's filters (the manual's Military, Events and Economic buttons) *(guessed)* |
| `STARTINGSEED` | the random seed of the last game (the Ultimate Editor: read by `trek.exe -n`) *(published)* |
| `Version` | "1.0.2", the version the main menu draws |

The setup keys match, value for value, the `last.sav` the user's install
had beside them: an elliptical galaxy (1), small (0), no minor races,
simple difficulty, every empire at Beginning, random events off, tactical
combat automatic. Reluva starts the options screen and a new game's setup
from them.

The game writes the file itself: the copy after a game has its keys in
another order than the installer's, `MONO` and the installer's ^Z gone,
the options' and the setup's keys added. Reluva writes it back as the game
would, when the options are accepted (the volumes, `TOOLTIPS`, `AUTOSAVE`,
`RANDOMEVENTS`, the timers, `TACTICAL`) and when a new game's setup is
(those, and `SHAPE`, `SIZE`, `RACENUM`, `DIFFICULTY`, the `CIVLEVEL`s):
the game writes the file only when Accept is clicked (the user, in the
game). Another copy, after a game set up as the Federation's on a ring
galaxy of medium size, many minor races, Normal difficulty, Early and
manual combat, has `SHAPE=2`, `SIZE=1`, `RACENUM=3`, `DIFFICULTY=3`,
`CIVLEVEL1`-`5=1`, `TACTICAL=DETAILED`, and a new `STARTINGSEED`: the
values as `gameInfo` holds them.
It changes only those keys: one already there keeps its line, spelling
and place; one that isn't goes where the game's own order (the played
copy's, above) puts it. Every other line, the line endings and anything
after the last stay as they are; the game's whole rewrite, with defaults
for the keys Reluva doesn't know, isn't copied. Without the file, none is
made.

### Not needed by Reluva

- In the install: `trek.exe` and `Trek.icd`; MicroProse's renderer
  (`mpr*.dll`, `gen.mpr`); Miles Sound System (`Mss32.dll`, `Mss16.dll`,
  `mssb16.tsk`); `Msvcrt.dll`, `dxreg.dll`, `Msdaemon.dll`, `mprtest.exe`,
  `mpsinfo.exe` and `Mpsinfo.txt`, `colormod.exe`, `hsbr.exe`,
  `Uninst.isu`, `msnzone.url`, `Crash.log`, `disk.id`, `readme.doc`,
  `tutor1.pdf`.
- On the CD: SafeDisc's files (`00000001.TMP`, `clcd16.dll`, `clcd32.dll`,
  `clokspl.exe`, `drvmgt.dll`, `secdrv.sys`, `dplayerx.dll`, `*.016`,
  `*.256`); InstallShield's (`data1.*`, `_sys1.*`, `_user1.*`,
  `_INST32I.EX_`, `_ISDel.exe`, `setup.*`, `layout.bin`, `SETUPDIR`, and
  `lang.dat` and `os.dat`, its language and platform lists); DirectX 6
  (`redist`, `DX6INST.DLL`, `dx6inst.exe`, `dxregtst.exe`); `AUTOPLAY.EXE`,
  `autorun.inf`, `ar*.exe`, `test.wav`, `stbof.BMP`, `trek.ico`.

## In Reluva

- **Recognising the game** (`botf.cpp`): the English release by
  `stbof.res` and `english.snd`, the German one by its `stbof.res` and a
  `german.snd` of any hash; a folder with `stbof.res` and `sfx.snd` that
  matches neither is offered as an unknown release.
- **Finding a file** (`GameFiles`, `game_files.cpp`): in the game's
  folder, then its language folder, then the folder `Stbof.ini`'s `CDPATH`
  names and its language folder, each ignoring case. So a minimum install
  finds `music.snd` on the CD, or beside the install if it's been copied
  there. Without `Stbof.ini`, the language is English and there's no CD.
- **Missing files:** `stbof.res` is needed; the game runs without
  `alt.res`, the sound archives and the movies, and the viewer shows what
  it finds.

## Sources and checks

- **Where the facts come from:** directory listings of an English retail
  install (a minimum install) and of its CD, and
  `tools/botf-re/botf_sample.py`'s report on them (sizes, hashes, first and
  last bytes, the zip listings), October 2026; the German `stbof.res` from
  the same CD.
- **How it was checked:** finding files is checked by
  `botf.unit.archives` ("game files"), on a made-up install with a
  `Stbof.ini` and a CD folder; detection by `botf.unit.detection`.

## Open questions

- The German release's `german.snd`, to hash it.
