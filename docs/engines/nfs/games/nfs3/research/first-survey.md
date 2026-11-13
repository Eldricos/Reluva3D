# Need for Speed III: the first survey (working note)

A working note from October 2026, the first study of the retail files and executable; the facts in it have since gone to their pages ([formats/](../formats/), [systems/](../systems/)), which say more.

## What the data holds

Identified from file headers and the strings in the executable. Formats
marked *public* have community write-ups to check against (see
**Sources**).

| Files | What | Format notes |
|---|---|---|
| `*.viv` | archives | `BIGF`, big-endian count, entries (offset, size, name). *Public.* Cars: `carp.txt`, `car.fce`, `car00.tga`, `fedata.*`, `dash.qfs`, `car.bnk`... |
| `*.qfs` | images | RefPack-compressed (`10 FB`) SHPI. *Public.* |
| `*.fsh` | images | `SHPI` / `GIMX` shape banks, named entries, palettes. *Public.* |
| `*.tga` | car skins | plain TGA (`common/image` reads them) |
| `*.fce` | car and object models | parts, vertices, triangles, texture pages. *Public.* |
| `trkNNN/trNN.frd` | track geometry | blocks, polygons, objects, textures by number. *Public.* |
| `trNN.col` | track collision and the virtual road | `COLL`, version 11. *Public.* |
| `trNNN.qfs`, `trNNN_8.qfs` | track textures | two texture sets (`_8`: 8-bit?) |
| `trNN00.can`... | replay and attract camera paths | binary |
| `3trNN[n][w].hrz` | sky and fog per condition (day/night/weather) | commented text |
| `trNN.ccm`, `simhts.dat`, `speeds[fr].bin`, `tut*.bin` | cameras, heights, AI speed tables (forward/reverse), tutorial | to study |
| `carp.txt` | car performance | **labelled text**: `mass [kg](2)`, `number of gears(3)`...; where it goes in [physics.md](../systems/physics.md) |
| `*.bnk` | sound banks | `BNKl`, entries with `PT` headers (EA ADPCM). *Public.* |
| `*.mus` + `.map` + `.lin` | interactive music | `SCHl`/`SCCl`/`SCDl` streams; `.map` is `PFDx`, EA's "Pathfinder" map, choosing segments as the race goes |
| `*.mad` | movies (CD) | `MADk` chunks, 320x240. *Public.* |
| `FeData/menus/*.mnu` | the front end's 80 screens | **INI-like text** (`[text] x=320 y=47 ...`) |
| `FeData/text/text.*` | UI strings, 7 languages | offset table, then strings |
| `*.ffn` | fonts | `FNTF` |
| `DashHud/*.pos` | HUD layouts | text ("RacerDigitalSpeedometer: (x,y) - ...") |
| `replay.rp3`, `ghost.gst` | a replay and a ghost lap | binary, same header |

The nine tracks (`trk000`-`trk008`) are, in order, Hometown, Redrock
Ridge, Atlantica, Rocky Pass, Country Woods, Lost Canyons, Aquatica,
Summit and Empire City, the bonus track
([tracks.md](../formats/tracks.md)). Backward, mirrored, night and
weather are flags, not other numbers.

## The executable's structure

The executable keeps its graphics and sound behind two small DLL
interfaces:

- **THRASH, the renderer.** `d3da.dll` (Direct3D), `voodooa.dll` and
  `voodoo2a.dll` (Glide) and `softtria.dll` (software) all export the
  same 34 functions:
  - `THRASH_init`, `setvideomode`, `pageflip`;
  - `talloc`, `tupdate`, `settexture`, `setstate`;
  - `drawtri`, `drawtrimesh`, `drawquad`, `drawtristrip`...;
  - `lockwindow`, `readrect`, `writerect` (2D);
  - and so on.

  The game chooses one at start-up (`3dSetup` writes the choice).
- **iSND, the sound.** `eacsnd.dll` exports 16 functions
  (`iSNDdirectstart`, `iSNDdirectplay3d`, `iSNDdirectserve`...) over
  DirectSound. The executable checks its version (`iSNDdllversion`
  returns `0x00100003`).
- **The executable itself** imports little graphics or sound: DirectDraw
  (create, enumerate), DirectInput (create), and WINMM's multimedia timer
  (`timeSetEvent`). It also imports threads, files, the registry, WSOCK32
  and TAPI (network play). In all it has 168 imports Reluva's Win32
  process doesn't stand in for yet.

### First run in Reluva's x86 interpreter (October 2026)

Tried with `reluva-tool x86-run`:

- `nfs3.exe` loads, relocates and has its imports bound. It stops 14
  instructions into the Watcom start-up, at `push es` (opcode `06`,
  not implemented; segment pushes are no-ops in a flat model).
- `d3da.dll` stops at the same instruction in its start-up.
- `eacsnd.dll` loads, and `_iSNDdllversion@0` returns `0x00100003`.
