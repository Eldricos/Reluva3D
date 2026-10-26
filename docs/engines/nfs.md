# The Need for Speed engine (plan)

Engine id `nfs`, to live in `src/engines/nfs/`. First game: Need for
Speed III: Hot Pursuit (`nfs3`).

Status: **Phase 2 done**:

- the engine detects the game and finds its folders;
- it reads the archives, pictures, text and fonts;
- it reads the car models, tracks, virtual roads and skies;
- it shows them as a data viewer, the tracks and cars in 3D.

This page records what
the retail data holds, what was tried, and the order the work should go
in. It becomes the engine's page as work lands, as `idtech3.md` is for id
Tech 3.

## Why one engine for a family

NFS III and NFS: High Stakes (1999) share most of their file formats:
VIV archives, QFS/FSH images, FCE models, FRD tracks, and EA's audio
banks and streams. Like id Tech 3, what they share is written once, and
what differs is keyed off a `GameInfo` entry. Code for EA formats that
other EA games of the period also use (RefPack, SHPI, BIGF, ASF/BNK
audio, MAD video) starts in the engine. It moves to `src/common/` once a
second engine needs it.

## The copy studied

Studied October 2026, from a US "Need for Speed III Classics" PC CD and
an install made from it.

- **Executable.** `nfs3.exe` is 1,589,248 bytes, built 3 September 1998
  (PE time stamp 904846030), version resource `3, 0`. The install's copy
  and the CD's are identical. It was built with Watcom C/C++ (its runtime's
  copyright string, sections `AUTO`/`DGROUP`). It has 46,398 relocations.
- **Install** (239 MB). Holds `GameData` (tracks, cars, sounds, speech,
  HUD) and `FeData` (front end), the executable and its DLLs.
- **CD** (468 MB without the DirectX, 3dfx and ad folders). It also holds:
  - the music, `gamedata/audio/pc/*.mus|.map|.lin`;
  - the movies, `fedata/movies/*.mad`;
  - Spanish speech, and more car speech and description banks.

  `install.win` lists `D:\GameData\Audio\pc\` as the music path, so the
  game reads music (and movies) from the CD.
- **CD check.** The executable calls `GetVolumeInformationA`. The CD's
  label is `NFS3`.

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `nfs3.exe` | `5cbf7d80f6b993926f7b41a0aa3ffc99` | 1589248 |
| `GameData/Tracks/trk000/tr00.frd` | `42ac1185877f2c7fbaa0de8c7b0d6351` | 1422739 |
| `FeData/text/text.eng` | `9295f24e9ca2d3b193fc39ec2429d7cd` | 35402 |
| CD `gamedata/audio/pc/alpirock.mus` | `612967aa448ea3538b53e63e2061b3dc` | 14376248 |

All nine tracks' `.frd` files are the same on the CD and in the install.

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
| `carp.txt` | car performance | **labelled text**: "mass [kg](2)", "number of gears(3)"... |
| `*.bnk` | sound banks | `BNKl`, entries with `PT` headers (EA ADPCM). *Public.* |
| `*.mus` + `.map` + `.lin` | interactive music | `SCHl`/`SCCl`/`SCDl` streams; `.map` is `PFDx`, EA's "Pathfinder" map, choosing segments as the race goes |
| `*.mad` | movies (CD) | `MADk` chunks, 320x240. *Public.* |
| `FeData/menus/*.mnu` | the front end's 80 screens | **INI-like text** (`[text] x=320 y=47 ...`) |
| `FeData/text/text.*` | UI strings, 7 languages | offset table, then strings |
| `*.ffn` | fonts | `FNTF` |
| `DashHud/*.pos` | HUD layouts | text ("RacerDigitalSpeedometer: (x,y) - ...") |
| `replay.rp3`, `ghost.gst` | a replay and a ghost lap | binary, same header |

The nine tracks (`trk000`-`trk008`) match the nine front-end track images
(`aqua atla coun empi home lost redr rock summ`). Which number is which
track is to be confirmed from `text.eng`.

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

## Two ways to play the game

**A. Native.** Read the data and write everything in Reluva: the front
end (from the `.mnu` files), the race, physics (from `carp.txt`), AI
opponents, traffic, the police, replays. This is how New Worlds is done.

- For: portable and moddable, and no Win32 work.
- Against: NFS III's handling, AI and pursuit behaviour would have to be
  matched by observation alone. That's the bulk of the game, and the hard
  part to get right.

**B. Hosted.** Run the retail `nfs3.exe` in the x86 interpreter, as
Elite Force's single-player DLLs are run (`docs/engines/x86.md`):

- THRASH is implemented natively on `gfx::Renderer3D` (a GPU backend
  for the game's own draw calls).
- iSND is implemented natively on `common/audio`'s mixer.
- DirectInput and DirectDraw are stood in for as the minimal COM objects
  the game uses, and the Win32 calls it makes are stood in for.

For: the original handling, AI and front end, exactly. Against:

- The Win32 work: threads, the multimedia timer, files, the registry,
  COM.
- Watcom's run-time conventions.
- Speed: the interpreter does about 50 million instructions a second.
  The game logic's needs are unmeasured. The original ran on a 166 MHz
  Pentium with a 3D card, but that figure includes rendering, which
  THRASH would take native.

**Recommendation: build the shared data layer and viewers first (needed
either way), then do a short hosted feasibility spike, and choose at a
gate.** The THRASH/iSND split makes B unusually tractable. B also keeps
the game's own behaviour, which is what players remember. A remains the
fallback, reusing all of the data layer.

## Plan

Each phase ends with something runnable and with its findings written
under `docs/formats/nfs/`, as CONTRIBUTING.md asks.

### Phase 0: skeleton and detection (done)

What was built:

- `games.cpp` holds the game list and both detection tables.
- `folders.cpp` finds the CD's files, in this order:
  - the target's `cdpath` setting;
  - the install itself;
  - a folder beside the install with `nfs3.exe` and `GameData/Audio/pc`.

  It also lists the tracks and cars.
- Engines now get their target's whole config section as
  `GameTarget::settings`, which is how `cdpath` reaches the engine.
- The engine shows an info screen: the folders, the music and movies on
  the CD, and the tracks and cars.
- `tests/test_nfs.cpp` tests detection and folder finding on folders it
  builds itself.

Checked on the retail copy:

- the install and the CD are each detected as the Classics release;
- the install finds the CD beside it (9 tracks, 16 cars, 19 music
  streams, 4 movies).

What was planned:

- `src/engines/nfs/` with `NfsMetaEngine`, a `GameInfo` table (`nfs3`;
  `nfs4` later), the `RELUVA_ENGINE_NFS` CMake option, and registry entry.
- Detection:
  - the table above;
  - a fallback by file names (`nfs3.exe` and
    `GameData/Tracks/trk000/tr00.frd`) for other releases;
  - an optional CD folder, configured or found beside the install, for
    music and movies. Without it the game runs silent, with no movies.
- Done when the launcher's *Add Game...* finds the install, and
  `reluva <target>` opens an empty NFS window.

### Phase 1: archives, images, text (done)

What was built, with each format written up in `docs/formats/nfs/`:

- **Readers** for each format, checked against every retail file:
  - [viv.md](../formats/nfs/viv.md): archives, as an `Archive`;
  - [refpack.md](../formats/nfs/refpack.md): `.qfs` compression;
  - [fsh.md](../formats/nfs/fsh.md): pictures;
  - [text.md](../formats/nfs/text.md): the front end's string tables;
  - [ffn.md](../formats/nfs/ffn.md): fonts, drawn into RGBA surfaces.
- **`game_files.cpp`** indexes the install, then the CD's files the
  install lacks, and the contents of every `.viv`.
- **Viewer pages** (`viewers.cpp`): Info, Textures, Strings (in the
  game's own font) and Fonts.
- **`reluva-tool` commands:**
  - `viv-list`, `viv-extract`;
  - `fsh-info`, `fsh-dump`;
  - `text-dump`, `ffn-render`;
  - `nfs-survey`, which reads everything under the folders given;
  - `nfs-page`, which draws a viewer page to a TGA without a window.
- **Unit tests** on hand-built data for every reader.

`nfs-survey` on the install and the CD reads 115 archives, 694 picture
banks (15,990 pictures), 16 text tables and 16 fonts, with no failures.
The exception is the Summit's two 8-bit texture sets
(`trk007/tr07?_8.qfs`). They're compressed with another EA scheme
(`32 FB`), which isn't read yet. The 16-bit sets of the same textures
read fine (see [refpack.md](../formats/nfs/refpack.md)).

What was planned:

- VIV reader as an `Archive`, so it joins a `SearchSet` like a pk3.
- RefPack decompressor, SHPI/FSH decoder (palettes, the 8/16/32-bit
  types used), `text.*` string tables, `FNTF` fonts.
- `reluva-tool viv-list`, `viv-extract`, `fsh-dump` (to TGA).
- Unit tests with small synthetic files (RefPack's three copy forms, a
  hand-built BIGF).
- Viewer pages: **Textures** (every FSH/QFS with its name), **Strings**,
  **Fonts**.
- Done when every `.viv`, `.qfs` and `.fsh` in the install and on the CD
  opens without error (a `survey` command reports it).

### Phase 2: models and tracks in 3D (done)

What was built, with each format written up in `docs/formats/nfs/`:

- **Readers**, each checked on every retail file (`nfs-survey`):
  - [fce.md](../formats/nfs/fce.md): car and object models;
  - [frd.md](../formats/nfs/frd.md): track geometry;
  - [col.md](../formats/nfs/col.md): the virtual road and moving
    objects' paths;
  - [hrz.md](../formats/nfs/hrz.md): sky settings.
- **The Cars page** draws every model with its skin and detail levels.
  The levels go by a part's place, as the names can't be trusted.
- **The Tracks page** draws each track. It shows:
  - the road at three details, roadside and placed objects, and lane
    markings;
  - the baked vertex lighting, scaled by the sky's ambient light;
  - cut-outs and additive glows;
  - the sky dome and horizon picture in four variants;
  - the moving objects animated along their keys (Hometown's water
    wheel turns at its mill);
  - the virtual road's walls as a guide.
- **Tools:** `reluva-tool fce-info`, `fce-obj`, `frd-check`, `frd-obj`.
  `nfs-survey` now also reads models, tracks, `.col` and `.hrz` files.
- **Unit tests** on hand-built data for each reader.

Checked under Xvfb with Mesa's software Vulkan driver (llvmpipe), not on
a GPU. Text on billboards and the front end's "NFS" logo reads the right
way round, which confirms that the data is left-handed and mirrored as
it should be.

Not done yet:

- comparing with screenshots from the original, which this phase's
  "done" asked for. There's no original to run here, so the checks were
  internal: consistency, text reading correctly, walls along fences;
- fog, clouds and the sun;
- the records in each track block (positions, lights, sounds);
- the `Render/pc` models' `.art` textures;
- the 8-bit texture sets.

What was planned:

- FCE reader (car parts, damage and dummies), shown on a **Cars** page.
  Cars get their `car00.tga` skin, and traffic and police get theirs.
- FRD reader (blocks, polygon chunks, objects, texture ids), COL (virtual
  road, collision), HRZ (sky, fog). A **Track** page flies through each of
  the nine tracks on the GPU renderer, with the sky, day/night and
  weather variants, and the track's objects.
- `reluva-tool frd-check` validates all nine tracks.
- Done when all nine tracks and all cars draw correctly against
  screenshots from the original.

### Phase 3: sound and video

- BNK banks and EA ADPCM decoder, into `common/audio`. The ASF stream
  (`.mus`) and the Pathfinder `.map`'s segment choices, for music that
  follows the race.
- MAD video decoder for the four CD movies (titles, demos).
- Pages: **Sounds** (banks, speech, engine samples), **Music**,
  **Movies**.

### Phase 4: hosted feasibility spike (time-boxed, about 2 weeks)

1. **The interpreter.**
   - Segment pushes and pops as no-ops.
   - Whatever else the Watcom runtime needs; its start-up is the first
     test.
   - Stand-ins for the KERNEL32 and USER32 calls start-up makes: files on
     the game's folders, the registry from config, and
     `GetVolumeInformationA` answering for the configured CD folder.
2. **THRASH as native functions.** `nfs3.exe` loads a "DLL" whose exports
   Reluva provides, logging every call and drawing triangles to the GPU.
3. **Threads and the multimedia timer** in the emulated process:
   cooperative switching at native calls.
4. **Measure.** Instructions per frame in the front end and in a race.

Gate: if the front end draws and a race starts at playable speed (or is
within reach of a decoded-instruction cache), continue with B; if not, go
native (A).

### Phase 5B: hosted game (if the gate passes)

- Finish THRASH: texture formats, states, 2D rectangles, page flips at
  the window's size.
- iSND on the mixer (3D placement, rates, streaming).
- DirectInput keyboard, mouse and joystick, as the bindings the game's
  own Controls screen sets.
- Saves, records and config in the save folder.
- Later: network play (WSOCK32) to other Reluva players.
- Interpreter speed: decoded-instruction caching (listed under the x86
  interpreter's known limits).

### Phase 5A: native game (if not)

1. Front end from the `.mnu` files and `text.*`.
2. Single race.
3. Physics from `carp.txt`.
4. AI and traffic from the speed tables and the virtual road.
5. Pursuit.
6. Replays.

Each part is checked against recordings of the original.

## Sources

Allowed under CONTRIBUTING.md:

- **The retail data and executable** (this copy). Reverse engineering of
  the executable is for interoperability: the THRASH, iSND and Win32
  interfaces.
- **Public format write-ups.** The NFS modding community's documents on
  FRD/COL/FCE (Denis Auroux's NFS3 track and car format notes, as used
  by T3ED and FCEFinish), the MultimediaWiki pages on EA formats
  (RefPack, SHPI, SCHl/ASF, MAD), and fan wikis.
- **Open-source readers, as references only.** OpenNFS (MIT) and
  FFmpeg's EA demuxers and decoders (LGPL 2.1+) may be read to check our
  understanding of a format. No code is copied from them: Reluva's code is
  written from the format descriptions and the data (decided October 2026).

## Open questions

- The units of the moving objects' key delays (60 ticks a second is
  assumed), and their rotation sense.
- How many times the horizon picture repeats round the sky.

- The `32 FB` compression of the Summit's 8-bit texture sets, and
  whether anything Reluva runs will need those sets.
- The `0x7C` blocks on cockpit pictures (hot spots?), and the flare
  parameters in `0x6F` text blocks (`A0 B45 R24 ROTATE`).
- The font header's flags and the size field that's 11 bytes short in
  two fonts.

- Which track number is which track. Is `_8.qfs` the 8-bit (software
  renderer) texture set?
- Whether the Classics release differs from the original 1998 retail in
  data or code. Other releases (UK, German, patched) need their own
  hashes.
- How the game uses `timeSetEvent` and `CreateThread`: sim ticks on a
  timer thread, or only audio and CD streaming?
- The THRASH structures (vertex and texture formats, state ids): from
  the calls the game makes in the spike.
