# Jedi Knight: Dark Forces II

Game id `jk`, on the [Sith engine](../../README.md); its code is in
`src/engines/sith/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| US retail, two CDs (English, Windows), unpatched; `JK.EXE` linked 11 September 1997 | `JK.EXE`, `Resource/Res2.gob` and `Episode/JK1.GOB` (`detectionTable`, `src/engines/sith/games.cpp`) | Recognised and imported from its CDs, its CDs found |
| Other releases (other languages, patched) | The same three files present (`fallbackTable`): "unknown release" | Not checked |

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `JK.EXE` | `6c5bc033d5f00c44df5e7107183b567d` | 1387520 |
| `Resource/Res2.gob` | `d3c4718f2e013021e2b80c94fb6641a9` | 56069075 |
| `Episode/JK1.GOB` | `6dea81c8e0dad25194d5008539e4eb34` | 34861164 |

### The install and the CDs

The installer copies the game from CD 1's `GAMEDATA` folder: the files
are the same bytes in both places (whole-file MD5s compared).

| In the install | On the CDs | Notes |
|---|---|---|
| `JK.EXE`, `SMACKW32.DLL` | CD 1 `GAMEDATA/EXE/` | The game, and RAD's Smacker player it calls |
| `Resource/Res1hi.gob` | CD 1 `GAMEDATA/MININSTALL/RES1HI.GOB` | The interface's pictures and fonts, sounds and speech; CD 1 also has `RES1LOW.GOB`, the same 820 names in less space |
| `Resource/Res2.gob` | `GAMEDATA/RESOURCE/RES2.GOB`, on both CDs | Textures, models, animations, scripts, AI |
| `Episode/JK1.GOB`, `JK1MP.GOB`, `JK1CTF.GOB` | `GAMEDATA/EPISODE/`; CD 2 has them too | Single player (21 levels), multiplayer, Capture the Flag |
| `Controls/*.CTL` | CD 1 `GAMEDATA/CONTROLS/` | Control set-ups for 13 joysticks and pads |
| — | `GAMEDATA/RESOURCE/VIDEO/*.SMK`: 8 on CD 1, 17 on CD 2 | The cut scenes, read from the CD while playing ([Smacker](../../../../common/video/smacker.md)) |
| — | `GAMEDATA/RESOURCE/JK_.CD` | Tells the CDs apart ([formats/jk-cd.md](formats/jk-cd.md)) |

`Jedi.EXE` in the install and on CD 1 is only the launcher (an MFC
program of 309,760 bytes): it starts `JK.EXE`, the installer, DirectX's
installer and the readme.

## Where it stands

- **Detection:** the US release is recognised from its install; other
  releases by their files' names.
- **The CDs:** found for an install, and told apart; *Import Game...*
  makes an install from them. Nothing reads them yet.
- **The archives:** the install's `.gob` archives are read
  ([formats/gob.md](formats/gob.md)), and in them every colormap,
  picture, material, font, string table, sound, sound class, episode,
  model, animation, puppet, sprite and particle cloud
  ([formats/](#this-folder)).
- **The cut scenes:** every one on the CDs decodes, picture and sound
  ([formats/smk.md](formats/smk.md)).
- **The data viewer** (`reluva-viewer`) shows the pictures, materials,
  colormaps, models, fonts and string tables, plays the sounds, the sound
  classes' sounds and the cut scenes, and lists the episodes' steps.
- **Not yet:** the levels and scripts, and the game. Plans, phases and decisions:
  [PLAN.md](PLAN.md).

## Using it

Add the game's install in the launcher (*Add Game...*), or make one from
the CDs with *Import Game...* on CD 1 (or a copy of it): it copies what
the game's installer does, and CD 1's cut scenes into `cd1`; *Add other
disc...* adds CD 2's into `cd2`. `reluva-tool import <CD 1> <folder>
-patch <CD 2>` does the same. Reluva looks for each CD's files (for the
cut scenes), the first of:

- the folder the `cd1path` or `cd2path` setting in the game's section of
  `reluva.ini` names (relative to the install, or absolute);
- a folder in the install (as `cd1` and `cd2`);
- a folder beside the install, whatever its name.

A folder is taken for a CD by its `GAMEDATA/RESOURCE/JK_.CD`, so a copy
of the whole CD, or of just its `GAMEDATA/RESOURCE` folder, will do.
`reluva-tool sith folders <install>` shows what's found. The game itself
shows an empty screen for now; Esc goes back to the launcher.

The data viewer is `reluva-viewer` (`reluva-viewer <its name in the
launcher>`, or `reluva-viewer <the install>`). Tab moves between its
pages, Esc leaves it, and on each page Left/Right step through the files
(PageUp/PageDown by ten, Home/End to the ends):

- **Pictures:** every `.bm` and its cels. C steps through the palettes a
  picture without its own may take (the interface's, each colormap's,
  each menu background's), K shows the transparent colour or not.
- **Materials:** every `.mat` and its cels. C steps through the
  colormaps, `[` and `]` the light levels, M the mip levels, K the
  transparency.
- **Colormaps:** the palette, the light levels from dark (top) to bright,
  and the blend table.
- **Models:** every `.3do`, drawn in 3D through a colormap. A/D and W/S
  turn about it, `+` and `-` go nearer and further, C steps through the
  colormaps. K steps through the animations that fit the model, Space
  plays or pauses one, `.` steps a frame.
- **Sprites:** every `.spr`, then every `.par` particle cloud. `.` steps
  through the material's cels, A/D and W/S turn about a cloud, C steps
  through the colormaps.
- **Fonts:** the glyph strip and sample text; `+` and `-` its size.
- **Strings:** every string table; Up/Down and PageUp/PageDown scroll.
- **Sounds:** every `.wav` and its waveform. Space plays or stops it, A
  plays each as it's picked.
- **Sound classes:** every `.snd`, its events, sounds and numbers.
  Up/Down pick an event, Space plays its sound.
- **Episodes:** each episode's steps; Up/Down scroll.
- **Cut scenes:** the cut scenes on the CDs found, each with the episode
  steps that play it. Space plays or pauses one with its sound, `.` steps
  a frame while paused.

`reluva-tool sith page <install> <page> [keys...] <out.png>` draws a page
after the keys given, without a window.

## This folder

| Page | What's in it |
|---|---|
| This page | The releases, where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | The game's own files, a page each: [gob.md](formats/gob.md) (archives), [cmp.md](formats/cmp.md) (colormaps), [bm.md](formats/bm.md) (pictures), [mat.md](formats/mat.md) (materials), [sft.md](formats/sft.md) (fonts), [uni.md](formats/uni.md) (string tables), [wav.md](formats/wav.md) (sounds), [snd.md](formats/snd.md) (sound classes), [episode-jk.md](formats/episode-jk.md) (episodes), [smk.md](formats/smk.md) (cut scenes), [3do.md](formats/3do.md) (models), [key.md](formats/key.md) (animations), [pup.md](formats/pup.md) (puppets), [spr.md](formats/spr.md) (sprites), [par.md](formats/par.md) (particle clouds), [jk-cd.md](formats/jk-cd.md) (the CD marker) |
| `systems/` | How the game behaves, a page per part |
| `research/` | Working notes |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |
