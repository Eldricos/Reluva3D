# The Need for Speed engine (plan)

Engine id `nfs`, to live in `src/engines/nfs/`. First game: Need for
Speed III: Hot Pursuit (`nfs3`).

Status: **Phase 3 done**:

- the engine detects the game and finds its folders;
- it reads the archives, pictures, text and fonts;
- it reads the car models, tracks, virtual roads and skies;
- it decodes the sound banks, the music and its maps, and the movies;
- it shows all of these as a data viewer, the tracks and cars in 3D, and
  plays the sounds, music and movies.

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

### Phase 3: sound and video (done)

What was built, with each format written up in `docs/formats/nfs/`:

- **Decoders**, each checked on every retail file (`nfs-survey`):
  - [bnk.md](../formats/nfs/bnk.md): sound banks. Their codecs are in
    `common/audio/ea_audio.cpp`:
    - EA-XA ADPCM;
    - EA's MicroTalk speech codec, bit-exact with a public-domain
      reference decoder on all 1,869 English speech sounds;
  - [mus.md](../formats/nfs/mus.md): the music streams, and the
    Pathfinder maps that lead from segment to segment by an intensity;
  - [mad.md](../formats/nfs/mad.md): the Madcow movies and their sound.
    All frames match FFmpeg's decode, apart from 21 high-quantiser frames
    where FFmpeg wraps 16-bit values. The audio matches sample for
    sample.
- **The Sounds page** lists every bank's sounds, shows the selected
  one's waveform and plays it.
- **The Music page** plays a stream from its first section. Each section
  leads to the next as the map's routes say for the intensity, which can
  be changed while it plays, or by the linear order (`.lin`).
- **The Movies page** plays the movies with their sound, which keeps the
  time. It can pause, jump and step frame by frame.
- **The engine** starts the mixer's audio output for the pages.
- **Tools:** `reluva-tool bnk-info`, `bnk-wav`, `mus-info`, `mus-wav`,
  `mad-info`, `mad-frames`, `mad-wav`. `nfs-survey` now also covers
  banks, streams, maps and movies.
- **Unit tests** on hand-built data, and the MicroTalk decoder against
  reference output.

Checked under Xvfb with SDL's dummy audio driver: the pages play and
move on as they should, but nobody listened to the output. The decoded
audio was checked as WAV files: spectrograms, and the comparisons above.

Not done yet:

- segments follow each other with a gap of up to a few milliseconds, as
  the mixer can't queue a sound to start when another ends;
- which section a race's music starts at, and what the maps' events and
  sections' first bytes do (the game's own player would tell);
- the banks' unknown header tags;
- the engine sounds' looping and pitch, which belong with the game.

What was planned:

- BNK banks and EA ADPCM decoder, into `common/audio`. The ASF stream
  (`.mus`) and the Pathfinder `.map`'s segment choices, for music that
  follows the race.
- MAD video decoder for the four CD movies (titles, demos).
- Pages: **Sounds** (banks, speech, engine samples), **Music**,
  **Movies**.

### Phase 4: hosted feasibility spike (done)

**The gate passes: the retail game runs in Reluva, front end and race,
and its own work is within reach of a decoded-instruction cache.
Continue with B.**

`reluva-tool nfs-host <game folder>` runs the retail `nfs3.exe` in the x86
interpreter (`src/engines/nfs/host/`, on `common/x86`'s Windows process):

- C: is the install folder; what the game writes goes to a folder of its
  own. D: is the CD.
- The registry holds the key the installer writes, naming the renderer.
- The game presents itself as running on Windows 98: it reads low memory
  to tell, as that allows.

What runs:

- **The game's own DLLs.** THRASH (`softtria.dll`, the software renderer)
  and iSND (`eacsnd.dll`) are EA's own code, run in the interpreter. They
  only use DirectDraw and DirectSound, which Microsoft documents, so
  those are what Reluva stands in for (`directx.cpp`). Nothing of EA's
  interfaces had to be guessed, which changes the plan's step 2:
  - DirectDraw surfaces in emulated memory; a flip shows the frame.
  - DirectSound buffers that play in time, though nothing is heard yet.
  - DirectInput's keyboard and mouse.
  - THRASH's calls are counted on their way through (`thrash.cpp`).
- **Threads and the multimedia timer.** Seven threads and one timer,
  scheduled as Windows would (x86.md). The game relies on its threads'
  priorities.
- **What happens.** Clicks from a script (`-keys`) choose *One Player*,
  *Single Race* and *Race*. The game then:
  - draws its front end;
  - loads Hometown with the Corvette;
  - counts down;
  - races, with the opponents driving off and the biplane flying over.

  Left alone at the menu, it plays its attract movie.

Fixed on the way, each found by the game going wrong:

- The interpreter: segment register instructions; zero-filled sections
  (Watcom's linker gives them a raw size); 64-bit FILD/FISTP kept exact
  (the game's `memcpy` copies 8 bytes at a time through the FPU, which
  broke every compressed file).
- The Windows process: executables, files, threads, the registry and
  version resources.

**Measurements** (Release build; the interpreter runs about 40 million
instructions a second):

| | Instructions a frame | Of them THRASH's (software drawing) |
|---|---|---|
| Front end menu | 5.1 million | 4.9 million |
| Race, Hometown, 8 cars | 16 million | 14 million |

At real speed that is 8 frames a second in the front end and about 2 in
a race.

The race is slower than real time: the game slows its race clock when
frames come slowly. Over 14.4 seconds on its race clock (74 seconds of
real time):

- 255 frames were drawn, 17.7 for each second of race time;
- outside THRASH, about 36 million instructions ran for each second of
  race time (2.0 million a frame).

That includes the sound DLL mixing in software. A fit over frame times
puts about 0.4 million instructions a frame on the frame itself, and the
rest on the simulation.

At 30 frames a second, the game's own work would therefore take roughly
40 million instructions a second, about what the interpreter does now.

A run measuring this directly didn't get there. It slowed the game's
clock (`-timescale`), but the race wasn't reached: under the slowed
clock, the second click doesn't take on the race-type dialog.

So:

- **The renderer.** The software renderer is most of the cost (77-89% of
  all instructions). THRASH has to be native: on the GPU, as the plan
  says, or at least a native software rasterizer.
- **The game's own work** is about at the interpreter's speed for 30
  frames a second. The decoded-instruction cache the plan lists (a few
  times faster, typically) is what makes it comfortable; it comes right
  after THRASH in 5B.

Not done in the spike (Phase 5B's work):

- THRASH native;
- the sound heard (iSND's buffers to the mixer);
- live input and a page in the engine;
- the game's movies and the d3da (Direct3D) renderer;
- saving.

### Phase 5B: hosted game (if the gate passes)

Started. Done so far:

- **THRASH's drawing, native** ([thrash.md](../formats/nfs/thrash.md)).
  `nfs-host -native` rasterizes the game's triangles, quads, lines and
  points in C++. It handles all four texture formats the game uses,
  culling, Gouraud shading, perspective-correct texturing, wrapping and
  the blend modes.
  - A front-end frame costs 0.5 million instructions, not 5.1 million.
  - A race frame costs 2.5 million, not 16 million.
  - Menu frames match softtria's to a mean of 1.7 of 255.
  - A race draws the same scene as softtria.
- **Sound to the mixer.** The game's sound DLL (iSND) mixes in software
  into one looping DirectSound buffer: 0.4 seconds of 22050 Hz 16-bit
  stereo. `GameHost::soundStream()` gives that buffer to Reluva's mixer
  as a stream (`audio::Stream`, new in `common/audio/mixer.h`):
  - the stream reads the buffer at its play cursor, with DirectSound's
    volume and pan;
  - it keeps its own steady pace, resyncing only if it drifts more than
    a tenth of a second from the cursor the game sees.

  Checked with `nfs-host -wav`, which pulls the stream every 10 ms as an
  audio device would. The front end's music (`show2.mus`) came out equal,
  sample for sample, to Reluva's own decode of the file. Each second
  recorded held exactly one second of the song: nothing skipped or
  repeated. The race's countdown beeps and engine are heard too.

  Placement, surround and slow frames were checked on 200-second runs
  into a race (`nfs-host -native -wav`):
  - **Placement.** iSND pans and places sounds itself, inside the mix it
    writes, and nothing on the way to the speakers mixes the two channels
    together: the mixer gives both the same gain unless the stream is
    panned. Measured in the stereo race, the side channel (left minus
    right) is 10 to 14 dB below the middle; and for the last 90 seconds,
    driving straight on, the right channel runs about 2 dB louder than
    the left.
  - **Dolby Surround.** The audio mode is one byte at 0xac of
    `FeData/config/config.dat`: 0 is stereo and 2 is Dolby. The file is
    loaded whole at 0x6fbb40, so the game's `audiomode` setting at
    0x6fbbec is that byte. Going by the order of its option names, 1 is
    mono and 3 is D3D/EAX, which would ask for DirectSound's 3D
    interfaces. The stand-in has none, and that mode hasn't been tried.
    Set to
    2 in a copy of the install, the race's mix has short spans where
    left and right are in opposite phase (a 50 ms correlation as low as
    -0.49; the stereo race's lowest is +0.16). That is how Pro Logic
    carries the rear channel, and it reaches the output unchanged.
  - **Slow frames.** `nfs-host -wav` counts the 256-byte blocks of the
    buffer that come round to be played again without the game having
    written them since. That happens only when the game stalls for longer
    than the 0.4-second buffer, as at a loading pause. DirectSound would
    play what the block held a loop before, which comes out as a
    stutter; the stand-in plays silence instead, for blocks of a
    streamed buffer (one written past its size; one written only once is
    left alone). Over a race: 69,529 blocks played and 9 stale, all at
    stalls; before the change, a half-second stall replayed 128 blocks
    as a stutter. Loading screens stall too, but the blocks there held
    silence already.
- **The Play page** (`play_page.cpp`). It runs the game on a thread of its
  own, started when the page is first shown:
  - frames are shown as the game flips them;
  - its sound goes to the mixer;
  - keys and the mouse go to it as DirectInput and window messages, Esc
    included; Ctrl+F5 goes back to the launcher;
  - the window holds the mouse while the page is shown
    (`System::setRelativeMouse`). The game draws its own pointer and
    moves it by the mouse's moves, one game pixel to each of the
    mouse's. Before, the window's pointer was turned into moves, which
    drifted from the game's once the window was resized;
  - saves and settings go to the game's save folder;
  - when the game ends itself (Exit Game, Yes), the page is finished
    (`Page::finished`) and the engine goes back to the launcher; if it
    stops with an error, the page stays to show it;
  - leaving the engine ends the process (`Win32Process::requestExit`)
    and waits for its threads.

  Ending, the game drains the keyboard as a console program would
  (`kbhit`, `PeekConsoleInputA`). The stand-in left the count of
  waiting events unwritten, so the game took it as a key waiting for
  ever and never ended. It now says there are none.

  The game plays its movies (320x240) in its 640x480 mode on every other
  line, leaving the lines between them black: every odd line of each
  movie frame is black. That's the game's own drawing (it only ever
  sets 640x480), so it looked that way on Windows too. The page fills
  them with the line above, for any DirectDraw frame whose odd lines are
  all black and whose even lines aren't. `movielines=keep` in
  `reluva.ini` keeps the original look.

  `nfs-page <game> <out.tga> play ...` drives it headlessly (wait, click,
  move, keys held). It reached a race (Hometown, a Corvette) from the
  menus by mouse, and holding Up drove the car to 63 mph. The game still
  runs slower than real time: the race clock showed 19 seconds after
  about 50.

- **Speed.**
  - **The interpreter** is 2.2 times faster ([x86.md](x86.md), Speed):
    memory accesses inline, one per-instruction test, and decoded ModRM
    operands cached.
  - **The native rasterizer** walks each row's span and steps its values
    along it. Before, it tested every pixel of a triangle's box.
  - **Page flips and clears** with the retail softtria are done natively
    ([thrash.md](../formats/nfs/thrash.md)).

  A race frame now takes 1.1 million instructions, against 2.5 million
  with native drawing alone. A race runs at 25 to 28 frames a second,
  its clock keeping about real time: it was 13 frames a second, and
  about 2 with softtria.

- **Saves.** The game writes its settings (`FeData/config/config.dat`),
  records (`FeData/stats/records.dat`) and the race's music copied off
  the CD (`GameData/temp.mus`) to the save folder. A second session reads
  them back from there. Checked through the Play page: Direction was set
  to Backward on the Location screen and a race started, which is when
  the game saves its settings. A new session's Location screen then
  showed Backward. Settings changed in the menus and not yet saved, by a
  race or by Exit Game, are lost if the page is left with Ctrl+F5, as
  they would be if the real game were killed.
- **The null device.** The Location screen opens `nul`, which now opens
  as an empty file (`win32_files.cpp`). Before, it ended the game.

- **Movies.** The game plays them itself, with its own MAD ("Madcow")
  player in the interpreter, drawn through DirectDraw. So the Play page
  no longer passes `-nomovie`. Checked on `titleav.mad`, the EA logo,
  Dolby trailer and title sequence, at 320x240 and 15 frames a second:
  - frames shown match Reluva's own decode of the file;
  - the game keeps pace, at about one shown frame to one movie frame
    (it drops a few);
  - the movie's sound, recorded with `-wav`, equals Reluva's decode of
    its EA-XA track sample for sample, one second of it for each second
    recorded.

  Esc skips the intro, as on Windows.
- **d3da (the Direct3D renderer)** isn't run, and needn't be. The game
  picks it only with a "3D Device Description" in the registry, which it
  gets from enumerating Direct3D devices. Otherwise it falls back to
  softtria, as it does in Reluva (`nfs-host -renderer d3da.dll`).
  Running d3da would mean standing in for Direct3D: devices, textures as
  surfaces, a Z-buffer and DrawPrimitive. Drawing THRASH's calls on the
  GPU directly, from the same calls the native rasterizer takes, gets the
  same pictures with no Direct3D at all.

- **THRASH on the GPU** ([thrash.md](../formats/nfs/thrash.md), On the
  GPU). With a GPU, the Play page draws the game's frames with it, by
  default at twice 640x480. Checked in the app under Xvfb on software
  Vulkan (lavapipe), driven by xdotool:
  - the intro movie shows over an empty frame;
  - the menus and a race are drawn on the GPU;
  - holding Up drove the car to 31 mph, the race clock keeping about
    real time;
  - at scale 2 the picture is the same, with continuous power lines and
    smoother edges.

  This was also the first run of the Play page in a real window, with
  the mouse. Mouse positions are taken in the game's pixels whatever
  the frame's size.

- **Texture uploads natively** ([thrash.md](../formats/nfs/thrash.md)).
  168 million instructions fewer in a run into a race; the time to load
  hardly changes.

- **Joysticks.** A game controller connected when the game starts is
  offered to it as a DirectInput joystick, under the controller's own name
  (`System::readGamepad`: SDL's gamepads, or else a racing wheel). The
  game sets DirectInput's standard joystick format (`c_dfDIJoystick`, read
  from its own data), then asks for each object by its offset. It is
  offered:
  - X and Y, the left stick;
  - Z, the triggers (left one way, right the other, centred at rest);
  - Rx and Ry, the right stick;
  - a hat, the d-pad;
  - twelve buttons.

  Axis ranges are as the game sets them (`DIPROP_RANGE`), with a 10%
  dead zone. The game then reads the keyboard only for what's bound to
  the joystick, as on Windows with one plugged in. `joystick=off` in the
  game's section of `reluva.ini` leaves the controller out.

  The game sorts a joystick into a kind by its DirectInput subtype (the
  `EnumDevices` callback at 0x436980, `dwDevType`'s second byte): a
  gamepad (4) is kind 4, a wheel (6) kind 2, any other joystick kind 1.
  Force feedback (`DIDC_FORCEFEEDBACK` from `GetCapabilities`) adds 8. Its
  default bindings come from the kind (0x4904f0), unless the name is one
  of the seven it knows (0x490640: SideWinder pads and wheel, WingMan
  Force, CH stick and wheel, Gravis GrIP), which have tables of their own:
  - a gamepad: steering on X, accelerate and brake on the first two
    buttons, the rest on buttons (0x490cb0);
  - a wheel or a joystick: steering on X, accelerate and brake on Y,
    accelerate forward (0x490e30, 0x490fa0).

  Those layouts apply when the bindings are reset; until then they're
  the ones saved in `config.dat`, which in the install are already
  steering on X and the pedals on Y. A gamepad's axes are read as on
  or off whatever they're bound to (0x43c000 makes any axis binding of
  a kind-4 device digital): with one offered as a gamepad, the stick
  pushed forward didn't move the car. So a pad is offered as a plain
  joystick (`DIDEVTYPEJOYSTICK_TRADITIONAL`), its sticks and triggers
  analog. Pushing the stick forward drove the car to 48 mph, and the
  game's Controllers screen can bind the triggers' Z axis instead.
  (The stand-in used to report a rudder, subtype 5, by mistake, which
  the game took the same way.)

  `nfs-host -joystick name` offers one, and its key scripts set its
  state (`ms:joy:1=-1` for an axis, `ms:joy:b0=1` for a button).
- **Wheels.** A wheel is offered as one (`DIDEVTYPEJOYSTICK_WHEEL`). Its
  pedals go together on Y, as wheels gave them then and as the game's
  wheel bindings expect: the accelerator pushes Y up (forward), the brake
  down. SDL reports some wheels as gamepads, with the pedals as the
  triggers. Others are only joysticks of SDL's wheel type; for those, the
  backend (`sdl_controller.cpp`) reads:
  - the wheel from the first axis;
  - the pedals from the first two axes after it found resting at an end
    of their travel (pressed, a pedal moves to the other end);
  - its buttons in order, and its hat as the d-pad.

  `wheelpedals=2,3` in the game's section of `reluva.ini` names the
  accelerator's and brake's axes instead, for a wheel whose clutch would
  be taken for one. `nfs-host -wheel` offers the joystick as a wheel:
  with the accelerator down (`joy:5=1`) the car drove to 67 mph.
- **Force feedback.** With a controller that can give forces (SDL
  haptics, or a pad's rumble), the joystick reports force feedback.
  - **What the game does.** Found by offering it and logging the calls.
    It resets the device (`SendForceFeedbackCommand`), sets the gain
    (`DIPROP_FFGAIN`), then creates four effects:
    - a constant force: jolts in collisions, up to 0.75, from either side;
    - a square wave: rumble at impacts and off the road, up to 0.38,
      with periods of 76 to 166 ms;
    - a sine: the engine, at 0.06, its period following the revs (83 ms
      at idle, down to 40 ms);
    - a spring: the wheel's centring, 0.71 at a standstill and 0.56 when
      moving.

    It changes them as the race goes (`SetParameters` with
    `DIEP_TYPESPECIFICPARAMS`). To a joystick rather than a wheel (a
    pad, as offered), it plays only the pushes and the spring: no
    vibrations, so a pad's rumble gives the collisions alone. Its Force Feedback screen has settings
    for stick volume, road effects, collisions, engine, body roll and
    road grip; they weren't tried.
  - **The stand-in.** It does DirectInput's side: `IDirectInputEffect`
    objects for the constant, ramp, periodic and condition effects. Each
    reads `DIEFFECT` and its type-specific parameters
    (`host/force_effects.h`: duration, gain, start delay, axes, direction
    in cartesian, polar or spherical form, envelope). `EnumEffects`,
    `GetEffectInfo`, `GetForceFeedbackState`, the device commands and
    `DIPROP_AUTOCENTER` are done too.
  - **What reaches the controller.** At any moment, what the playing
    effects add up to along X (`sumForces`) is one `GamepadForce`: a
    push, a spring, a damper and a vibration, in DirectInput's units and
    sign convention (the side a force comes from), which SDL's haptics
    share. The Play page passes it to the system when it changes
    (`System::setGamepadForce`).
    - On a wheel with haptics, the backend keeps an SDL effect of each
      kind running for ever and updates it: constant, spring, damper,
      sine or square, and auto-centring.
    - On a pad, the push goes to the heavy motor; the vibration goes to
      the light motor if its period is under 50 ms, else to the heavy
      one.

    `forcefeedback=off` in `reluva.ini` leaves it out.

  Checked with `nfs-host -ff -forcelog forces.csv`, which records the
  forces at each frame, on a race into a fence: the figures above are
  from it. The backend was checked on SDL's virtual joysticks
  (`tests/test_sdl_controller.cpp`): a wheel's axes, pedals and buttons,
  and a pad's rumble. No real wheel or pad was tried (the container has
  none), so how the forces feel is untested.
- **The game's Controllers screen.** Accelerate was rebound to W
  there, and in the race that followed, holding W drove the car to 50
  mph.

- **Native sound mixer** (`host/native_sound.cpp`).
  - **Why.** A race's frame rate in `nfs-host` is the interpreter's
    speed: in a race some thread can always run, so the processor never
    idles
    (`nfs-host` now reports idle time each second; it's 0). The hot spots
    of a race (`nfs-host -hotspots`) put about 27% of its instructions in
    the game's software sound mixer (iSND, linked into nfs3.exe):
    hand-written x87 loops.
  - **What it does.** Worked out from its code:
    - **Two resamplers** (0x527330 stereo, 0x5273c0 mono). They step a
      16.16 position through float samples, each result a*(1-t) + b*t, t
      the fraction's top 16 bits times 1/65536. The code takes those bits
      with FILD from a scratch copy of the fraction (0x56b138, 0x56b148),
      two bytes in, so the word after it is read too. The stereo one
      always makes at least one frame.
    - **Twelve mixers** (0x520aee to 0x521133). Six are for mono sources
      and six for stereo ones, one for each pair of levels: off, full or
      between. The game picks one as a voice's levels change (0x520a5e,
      0x520e1e). Each adds the source into a stereo float mix, each side
      scaled by its gain (level times 1/127), or as it is at full, or not
      at all.
    - **The mix to 16 bits** (0x521d00). It adds 1.5 * 2^23 to each float
      and stores the sum back over it, so the low mantissa bits are the
      sample rounded to the nearest; past 16 bits it clips to 32767 or
      -32768.
  - **Native.** Each is replaced with a native function, only where the
    routine's bytes hash to the retail code's
    (`GameHost::replaceRoutine`). They compute as the interpreter does
    the x87's (doubles, rounded to float where the code stores one), in
    the same order.
  - **Checked.** `reluva-tool nfs-native-check <game>` loads the game
    twice, once without native routines (`Options::nativeRoutines`), and
    runs each original and its native version on the same random inputs.
    The inputs cover counts from -3 to 40, fractions, steps, gains, and
    samples past the 16-bit range. Every byte written and every register
    came out the same in 300 trials each. With the gains swapped on
    purpose, it reported every mixer that uses them.
  - **The result.** On a race into a fence, the frames took 1.20 million
    instructions instead of 1.49 million, and the frame rate rose from
    18.2 to 20.4 a second here. The hot spots are now flat (the hottest
    block 1.1%).
  - **A correction.** Those runs drew in software (`nfs-host -native`),
    and a host-side profile (a sampling profiler, by source line) put
    about a third of their time in that drawing (`NativeThrash`). With
    the GPU's lists instead (`-gpulog`, as the Play page runs), the same
    race made about 60 frames a second at 0.85 million instructions
    each, the interpreter running 57 to 81 million a second. In the app
    on software Vulkan, the game made 59 to 61 frames a second; the
    driver drew about 20 of them. So in the app the interpreter keeps up,
    at least on a machine like this one.
- **How fast it goes, in the app.** Every 5 seconds the Play page writes
  a line to the console. It gives the frames the game made and the
  instructions a second, whether the GPU or software draws them, and how
  many were drawn, not needed (a later one covered them), dropped (too
  many waiting) and shown. That tells a slow interpreter from frames lost
  on the way to the screen.
- **The same run twice** (`nfs-host -deterministic N`). The clock
  becomes a count of instructions (N a millisecond), plus a millisecond
  each time every thread waits, so a run with the same keys goes the same
  way each time, whatever the host's speed. At the end the tool prints a
  hash of memory and registers. Two builds of the interpreter that agree
  on it ran the game the same; that is how each speed-up below was
  checked. The per-second line also says how much of the work was the
  game's main thread and how much the timer's.

- **Network play** (TCP). The game does its own networking through
  Windows Sockets 1.1 (`WSOCK32`, 25 functions by ordinal), with no
  DirectPlay.
  - **The stand-in.** `host/winsock.cpp` does each call on real sockets
    (`net::Sockets`, `common/net.h`, done for the system in the sockets
    backend). It covers Winsock's structures in the game's memory
    (`sockaddr_in`, `fd_set`, `hostent`, `WSADATA`), its error numbers,
    and its blocking: a socket blocks unless the game makes it
    non-blocking, and a blocked call (`accept`, `recv`, `select`) lets
    the game's other threads run while it waits.
    `GetSystemMetrics(SM_NETWORK)` says there's a network.
  - **What the game does.** Found by logging its calls.
    - **Probing.** Opening Connect Players, it asks for an IPX socket
      (`AF_IPX`, refused here, so that entry stays dim) and a TCP one, to
      see which it can offer.
    - **Hosting.** Each player listens on TCP port 1030 (peer to peer),
      having looked up its own address (`gethostname`,
      `gethostbyname`) to show it.
    - **Joining.** It asks for a domain name or IP address and connects
      to that player's port 1030.
    - **Messages.** Each one starts with its 16-bit length, itself
      included: the receiver peeks at 2 bytes, then at that many, then
      reads them.
    - **In a race.** Each player sends a 35-byte message about 29 times
      a second, about once a frame. Nothing used UDP.
  - **Two copies on one machine.** `nfs-host -net` gives the game the
    machine's sockets, and `-netaddr 127.0.0.2` gives a copy its own
    loopback address wherever the game asks for any. So two copies can
    both listen on port 1030, and what each sends comes from its own
    address. `-broadcast` names where a UDP broadcast goes instead;
    nothing used one yet.
    - **The test.** Copy A (127.0.0.2) chose Network (TCP), Host Game,
      Peer-to-Peer. Copy B (127.0.0.3) chose Join Game and typed
      127.0.0.2.
    - **Lobby.** Both reached the same multiplayer lobby, each listing
      both players. B's chat line ("hi") showed in A's chat, and B's
      Ready light in A's list.
    - **Race.** A started the race, and both loaded the same race of
      eight cars (two players and six of the game's). They ran it for
      four minutes, their race clocks a second apart, with no drop-out.
  - **Scripting the menus.** A test script clicks by sending the cursor
    to the corner and then moving it to the point: the game's cursor is
    the sum of the mouse's moves, clamped to the screen, without
    acceleration. `.` `,` `-` `/` and `;` can be typed (each key now
    gives its own character as `WM_CHAR`; before, the arrow keys gave
    `%&'(`).
  - **In the Play page,** the game gets the machine's sockets
    (`System::sockets`); `network=off` in `reluva.ini` leaves them out.
    Other players connect to TCP port 1030, so a router has to pass it
    on to the host.

- **Loading time.** Measured with `nfs-host -framelog` and
  `-hotspots from to`, which counts the instructions run in each
  16-byte block of code over a range of the run.
  - **Start-up.** After its first frame (about 2 seconds in), the game
    takes about 5 seconds to its next. It isn't waiting: those are about
    330 million instructions of its own loading code, interpreted. A key
    press doesn't cut it short. The time is spread out:
    - its C library's `strcmp`, about 8%, called 1.5 million times;
    - a 16-bit image-rectangle copy, about 7%;
    - the sound mixer's float-to-16-bit loop, about 4%;
    - Watcom's `__CHP` rounding helper, about 3.5%;
    - and a long tail.
  - **A track.** About 1.35 seconds and 85 million instructions. A
    quarter of them are `__CHP`, called a pixel at a time by the game's
    image scaling; another third are that scaling.

  Done about it:
  - `strcmp` and `__CHP` run natively. `GameHost::addNativeRoutines`
    patches a jump to a native function over each routine, where its
    bytes are as expected.
  - Native calls are cheaper to make: they're counted per slot, input
    is looked at only when some is queued, and the log's checks run only
    while logging.

  Start-up now takes about 300 million instructions to its second frame
  (4.9 seconds), and a track 72 million (16% fewer). The track's wait
  stays about 1.3 seconds, though, so part of it is spent elsewhere. Much
  more would take a faster interpreter core, translating whole blocks of
  code, rather than more routines run natively.

- **The GPU's frames against the software rasterizer's**
  ([thrash.md](../formats/nfs/thrash.md), Compared with the software
  rasterizer). Frame by frame, from the menus through a loading screen
  to a race, they now agree:
  - the menus within 0.5 of 255 on average;
  - the loading screen exactly;
  - the race within 1.1 once rounded to the software path's 5:6:5.

  The comparison found the GPU path dropping loading screens and the
  countdown's digits (culling the wrong way round), and frames that add
  to the last; both are fixed. The cockpit view has no rear-view mirror
  to compare; the outside views agree as the cockpit does.

Still to do:

- **Played by hand** on a real GPU with speakers, a pad and a wheel. So
  far it's been checked on software Vulkan and virtual controllers,
  driven by scripts.
- **Network play, the rest.**
  - Real machines over a LAN and the internet: so far only two copies
    on one machine have played.
  - How a race holds up over a slower link, or when a copy stalls for
    loading longer than the original did.
  - The messages written up as a format.
  - IPX (which could be carried over UDP), modem and serial links
    (a null-modem cable over TCP) aren't stood in for.
- **Not planned:** d3da (see above) and the Glide renderers, which the
  native and GPU drawing make unneeded.

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

- The sound header tags not understood ([bnk.md](../formats/nfs/bnk.md)):
  `0x92` (1 or 2, but not bytes per sample), `0x06` (25 on most sounds)
  and the rest.
- In the Pathfinder maps ([mus.md](../formats/nfs/mus.md)):
  - the sections' first byte;
  - the 16-byte events;
  - header byte 5;
  - which section a race starts at (Reluva starts at 0);
  - what the game feeds in as the intensity.
- In the movies ([mad.md](../formats/nfs/mad.md)):
  - whether the game's decoder keeps the quantiser in 16 bits, as FFmpeg
    does;
  - what the game shows for the demos' fade-in frames;
  - the frame header's unused fields.

- Which track number is which track. Is `_8.qfs` the 8-bit (software
  renderer) texture set?
- Whether the Classics release differs from the original 1998 retail in
  data or code. Other releases (UK, German, patched) need their own
  hashes.
- How the game uses `timeSetEvent` and `CreateThread`: sim ticks on a
  timer thread, or only audio and CD streaming?
- The THRASH structures (vertex and texture formats, state ids): from
  the calls the game makes in the spike.
