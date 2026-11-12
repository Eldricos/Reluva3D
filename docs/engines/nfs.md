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
| `carp.txt` | car performance | **labelled text**: "mass [kg](2)", "number of gears(3)"...; where it goes in [physics.md](../formats/nfs/physics.md) |
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
([tracks.md](../formats/nfs/tracks.md)). Backward, mirrored, night and
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
The Summit's two 8-bit texture sets (`trk007/tr07?_8.qfs`), compressed
with another EA scheme (`32 FB`, Huffman), were the exception until
October 2026; they're read now, checked against the game's own routine
(see [refpack.md](../formats/nfs/refpack.md)).

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
- **The Cars page** draws every model with its skin (its rows as the
  game's loader takes them, fce.md) and detail levels,
  its triangles' flags (two-sided, unlit) and the game's reflection of a
  track's sky ([fce.md](../formats/nfs/fce.md)).
  The levels go by a part's place, as the names can't be trusted.
  Its pictures are bilinear-filtered without mipmaps, as voodoo2a.dll
  has Glide draw all of the game's ([thrash.md](../formats/nfs/thrash.md),
  Texture filtering; `texturefilter=point` point-samples them). `RELUVA_NFS_CARS_VIEW="model yaw pitch distance"`
  opens it on a model, seen from there, for screenshots that repeat
  (checked so in November 2026: tail lights at the back and plates below
  them on the Corvette, F355, Diablo, police Diablo, CLK-GTR and XK8,
  the XK8's right-hand badge mirrored by the model's own mapping).
- **The Tracks page** draws each track. It shows:
  - the road at three details, roadside and placed objects, and lane
    markings, their pictures bilinear-filtered without mipmaps as the
    game's Glide renderer draws them (thrash.md, Texture filtering);
  - the baked vertex lighting, scaled by the sky's ambient light;
  - cut-outs and additive glows;
  - the sky in four variants, drawn as the game draws it: the sky's and
    earth's shading and the horizon picture as rows of flat quads in
    front of the camera, the sun and moon, clouds and lightning, the
    sky's top as the clear colour, through the game's view, 90° across
    and 72° up with its pixels 3 % taller than wide, and the fog of the
    dynamic fog region at the camera's road slice
    ([hrz.md](../formats/nfs/hrz.md)); `RELUVA_NFS_TRACK_CAMERA="x y z
    yaw pitch"` places the camera as a view of `nfs-host`'s;
  - the moving objects animated along their keys (Hometown's water
    wheel turns at its mill);
  - the virtual road's walls as a guide, with the road's edges from its
    lane counts and widths (key G);
  - the cars' effects (hits, hit smoke, wheel spray and smoke) run once
    per 64 Hz car step, as `car_impacts_task` does; particles of kinds
    1, 4, 10 and 11 within 3 units of the camera become splats on the
    lens (`0x4ca500`; [col.md](../formats/nfs/col.md)), mud splats
    playing Gen3d's slot 15 (`0x415010`); computer cars' verge clumps
    were checked in the wet;
  - the lights in their types' colours, sizes and blinking, from the
    game's table, each as its two quads: `glw0` at a fixed size on the
    screen fading out to 256 away, and `glw3` in perspective, only while
    the light's centre is on the screen ([frd.md](../formats/nfs/frd.md),
    Lights; checked against a night frame of Hometown's town centre in
    `nfs-host`); key Q steps the Special Effects option (High, Low,
    Off: no track lights, rain flares or fog beams);
  - animated textures (the river through Hometown's covered bridge),
    two-sided polygons, and see-through ones (trk002's and trk006's
    blue panels) blended half grey over what's behind, though not
    sorted by depth as the game sorts them;
  - additive pictures only where the text is `<additive>`, as the
    game's loader has it;
  - the placed sounds as the game plays them: one loop, random
    one-shots, each block's first sound only.
  - wall-hit particles (key P, Shift+P for a fast hit): at the
    virtual-road wall nearest the line of sight, the side's code picks
    sparks, dust or smoke as the impact code does, and they move at the
    game's 64 ticks a second by its rules and are drawn as it draws
    them: sparks as thin streaks, dust and smoke as turning pictures
    from `sfx.fsh` ([col.md](../formats/nfs/col.md), Particles). The
    car is made up (its speed 40 or 18, its centre a unit in from the
    wall) and the random numbers aren't the game's.
  - the blocks the game draws from the camera's, and its cut-off of
    polygons whose first corner is more than 500 ahead, by night too and
    for moving objects ([frd.md](../formats/nfs/frd.md), Which blocks
    are drawn);
  - by night, the headlights of a car at the camera as the game's pass
    lights them: `glw2` projected on polygons whose texture lacks flag
    `0x04`, the light's colour as a floor on the others' corners and on
    the placed objects' vertices, still (Hometown's lamp posts) and
    moving, at their place this frame, all fogged as the polygons are
    (checked against a night frame at Hometown's water wheel)
    (key E: off, low and high beams, high at first by night as a
    human's car starts; Shift+E: the projection off, as `nfs-host`'s
    `config.dat` has it; Ctrl+E: the game's test of each block against
    each light off); key J adds two Corvettes going the camera's way and
    a police Corvette coming with its siren on, their boxes on the road,
    driven as the computer drives, 64 steps a second: the speed tables'
    speeds by the driver's and track's factors and the catch-up (the
    camera's car the race's human), the acceleration table
    and brakes from each car's `carp.txt`, the lane votes and lane
    changes, keeping behind a slower car in the lane and flashing their
    lights when held up (Shift+J stops them; Ctrl+J puts them on the
    grid with the countdown, Ctrl+Shift+J cycles the laps, none, 1, 2, 3,
    after which they finish and coast to a stop; otherwise the page's cars are taken
    as past the race's start, where the flashing, the best line's and the
    empty lanes' votes begin), their drivers acting every
    8 steps and their model at 32 Hz as the game's, their lanes taken at
    the side offset as the game measures it (so a car at a lane's middle
    weaves between the two lanes as the game's do), moved across by the
    game's three controllers (pushed in the road's frame when fast,
    turned towards the point 1.7 s ahead when slow or sliding) and kept
    within the walls; the police car by the
    computer cops' driver, patrolling, on a run with its siren or
    chasing a racer that has been over the section's speed (+0x6F0),
    reversing when past a stopped one, holding a slow one (160-224 steps
    by its race distance) until it pulls over, stopping it with the
    officer on foot beside it (the track's `copN.fce`, on the road's
    polygons, lit as a car, track 8's with his red light) and giving the
    ticket; a second police car 800 m on, patrolling, for the chasing
    one's calls for help (a strip by the chase's heat, backup); key C
    orders the free one as the game's chasing cops order a free one, in
    turn to lay a spike strip at the roadside (racers steer round it; one
    whose wheels touch its quad is pulled over), to set a roadblock
    (across the road when no racer is within 240 m) and back to patrol,
    and Shift+C follows it with the camera (from past its strip while one
    is out, near the racer while stopping one); the info line shows each
    police car's state, its racer's and its last radio call (no speech).
    `RELUVA_NFS_TRACK_CARS=<dump>` places
    them as a dump of `nfs-host`'s car records has them (the human's car
    left out unless `RELUVA_NFS_TRACK_CARS_HUMAN` is set, but its
    headlights, the camera's car's, where the dump has it; an officer on
    foot the dump has beside a car, +0x8E4, is drawn where it puts him),
    `RELUVA_NFS_TRACK_CARS_SETUP=<dump>` (the race's set-up records at
    `0x6fd52c`) gives each its own model by its key, in its paint, and
    `RELUVA_NFS_TRACK_STEP=<step>` holds the moving objects at that
    game step, for frame-for-frame comparison. Their skins are recoloured
    by the paint as the game's loader does and the right way up
    ([fce.md](../formats/nfs/fce.md), The skin's paint). They are lit as the game
    lights a car (a near one by night also by the track's lights within
    16, `0x4b9550`), with its reflection of the sky's sphere map in the
    colours the game works out (key Z: off), their tail-light, headlight
    and roof-light glows (fogged and filtered as the scenery) at the game's depths (the body's nearest point,
    a roof light's own less 0.75; the tail lights brighter while braking, more
    than 5 m/s too fast, at bends), in the weather each glow's flare on a
    wet road, on the road's plane as the game puts it, and in fog over 30 % each headlight's beam, each car's
    shadow under it (`0x4b81a0`: `shad` of `sfx.fsh` in 0x6FFFFFFF on the
    road under its box, cast away from the light by day out of the
    weather), and they light
    the track by night after the camera's car, the roof light too
    ([frd.md](../formats/nfs/frd.md), Other cars' lights, The computer
    cars; [fce.md](../formats/nfs/fce.md), Lighting). Each is drawn at
    the detail its distance gives it, with that detail's parts, and with
    its triangles facing away left out unless two-sided, as the game
    does (November 2026; before, the high parts at every distance and no
    culling, which showed the wheel arches' insides as black wedges;
    fce.md, Facing away). A dumped car whose rear tyres skid makes the
    game's tyre smoke, and `RELUVA_NFS_TRACK_PARTICLES=<dump>` (the 600
    particle slots at `0x7ddfd0`) holds a dump's particles where they
    were; their pictures are the day's `SP` set by day and `SN` by night,
    fogged ([col.md](../formats/nfs/col.md), Tyre smoke, Over the cars).
    By day the smoke was about 10 levels brighter than in nfs-host's
    frames: the host truncated each blended layer to 5:6:5, where
    `voodoo2a.dll` has Glide dither 4x4; the host's native drawing now
    dithers, and the smoke is within 3-4 of the page's (November 2026;
    [thrash.md](../formats/nfs/thrash.md), Dithering; 3dfx's own
    documents don't give the dither's arithmetic, so its rounding stays
    open). The smoke's pictures are turned as the game turns them: the
    page had them mirrored, which spread the puffs over the car's front
    (col.md, Its spread over the car).
    The page starts with the z-buffer and the headlights' projection off,
    as nfs-host runs the game (Shift+E turns both on): its pool on the
    road then matches the hosted game's, and particles nearer than a
    car's middle are drawn over it as the game's depth order draws them;
  - in the weather variants, the rain or snow as the game makes and
    draws it (November 2026; [col.md](../formats/nfs/col.md), Rain and
    snow): the `.hrz`'s weather type, stretch of road and on-off cycle,
    up to 400 raindrops or 800 flakes in a box round the camera moved
    every other tick, hidden as they come back into it under cover,
    drawn through the pass's own 120° view, added, as `weather.fsh`'s
    streaks (`DRP1`) or squares (`DRP2`), and the splashes held on the
    screen when a drop comes within 3 of the camera (`SPL1`, `SPL2`);
    fewer at Special Effects Low and Off (key Q). Checked against the
    `Rain` and `Snow` clips (faint streaks and rings, white dots and
    grey splats, none falling in a tunnel);
  - the mirrored track (key U): texture set 1 and the view flipped left
    to right, its culling turned with it, as the game's mirror flag
    does; checked against a mirrored `nfs-host` race on Hometown.
- **The Cockpits page** shows each car's `dash.qfs`: the dash, the
  steering wheel, the F355's gear indicator, and the speedometer and
  tachometer needles where the picture's `0x7C` block puts them
  ([fsh.md](../formats/nfs/fsh.md), Gauges), all drawn as the game
  draws them: the needles' angles, the wheel turning with the steering
  and the speed. They sweep or are set with the keys. A frame from the
  game in `nfs-host` agrees with the page.
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
  internal: consistency, text reading correctly, walls along fences.
  Since then the sky has been checked against the hosted game's own
  drawing ([hrz.md](../formats/nfs/hrz.md)).

Added later: fog, clouds and the sun ([hrz.md](../formats/nfs/hrz.md)),
then drawn as the game draws them; and the records in each track block,
its lights and sounds ([frd.md](../formats/nfs/frd.md)).

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
  time. It can pause, jump and step frame by frame. It shows them as
  the game does, by its decoder and its display step at twice the size
  ([mad.md](../formats/nfs/mad.md)), which matches frames captured from
  the game; key G switches to its other modes or a plain conversion.
- **The engine** starts the mixer's audio output for the pages.
- **Tools:** `reluva-tool bnk-info`, `bnk-wav`, `mus-info`, `mus-wav`,
  `mad-info`, `mad-frames`, `mad-wav`. `nfs-survey` now also covers
  banks, streams, maps and movies.
- **Unit tests** on hand-built data, and the MicroTalk decoder against
  reference output.

Checked under Xvfb with SDL's dummy audio driver: the pages play and
move on as they should, but nobody listened to the output. The decoded
audio was checked as WAV files: spectrograms, and the comparisons above.

Compared with footage of the original game (18 YouTube races, November
2026): the decoded music segments are found in the clips, in the order
the maps and the game's rules give (which stream each track plays, the
zones, place, last lap, speed and pursuit: [mus.md](../formats/nfs/mus.md),
Seen in footage). The sirens, the dispatcher's speech (whose bank is
the track modulo 4) and the engine's pitch against the tach are in
[bnk.md](../formats/nfs/bnk.md), Seen in footage; the wall hits,
skids and moving objects' sounds couldn't be picked out.

Not done yet:

- (since done: the maps' events, parts and control value, as the game
  uses them, [mus.md](../formats/nfs/mus.md));
- the meanings of a few of the sound patches' parameters (bnk.md names
  the rest, and how an engine's pitch follows its revs).

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
  priorities. What each one does is in
  [nfs-threads.md](nfs-threads.md): the race simulation runs on the main
  thread, and the 128 Hz timer only counts ticks and wakes the sound,
  controls and tick threads.
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
  audio device would (then by the wall clock; now by the game's, below).
  The front end's music (`show2.mus`) came out equal,
  sample for sample, to Reluva's own decode of the file. Each second
  recorded held exactly one second of the song: nothing skipped or
  repeated. The race's countdown beeps and engine are heard too.

  `-wav` records by the game's clock (`GameHost::recordSound`). Each time
  the clock passes a 10 ms step (checked before each system call), the
  host mixes the frames up to the next step, in the emulation's own
  thread. Frame n is what plays at the game's millisecond n / 22.05.
  Where the clock jumps, the frames before it are silence. So a
  `-deterministic` run gives the same file each time, and two runs whose
  instruction counts differ still line up by the game's time. Pulled by
  the wall clock, as before, the file depended on the host's speed.
  Checked: two stereo races with the same keys (5 billion instructions,
  100 seconds of game time) gave identical files, 100.03 seconds long. The
  Play page is unchanged: it pulls `soundStream()` from its audio device.

  `-voices file.csv` logs what the game's mixer adds, each 100 ms of the
  game's clock, taken in the native mixers (`native_sound.cpp`): the
  energy going to L+R and to L-R, apart for voices whose two levels
  have the same sign (front) and opposite signs (behind, in Dolby). The
  mixer runs about 100 ms ahead of the recording (the best match of the
  two series' L+R, correlation 0.97).

  With `-mmx` the game mixes with its integer routines, in the
  interpreter, and the native mixers never run, so `-voices` logged
  nothing. Now `-voices` with `-mmx` puts a native function at the
  entries of the two 16-bit MMX mixers (`0x51f7ad` mono, `0x51f897`
  stereo; `GameHost::hookMmxMixers`, `native_sound.cpp`; the 8-bit
  ones play no retail sound). It reads the call's levels (words L × 256)
  and source samples, adds up what the call will add as `pmulhw` gives
  it, doubled as the mix down doubles it (so in the float path's
  units; the bus's saturation left out), then does the routine's first
  four instructions, whose 7 bytes it replaces, and goes on at the
  fifth: the mix stays the game's. It goes in only with `-voices`.
  Tested on the race above (`s6-mx1`, `-mmx -deterministic 50000`,
  `70000:+up`, 120 s) against the float path (`s6-fl1`): 1,104 windows
  (1,121 with floats), the sums against the recording's L+R correlate
  0.933 at the 100 ms lead (0.938 with floats), their energy is 0.55
  dB over the recording's from 72 s on (0.54), and the side against
  the middle is −12.85 dB in the sums and −12.86 dB in the recording
  (−12.91 and −12.92 with floats). The hook replaces four
  instructions, so the clock of instructions moves: the recording
  parts from that of `-mmx` without `-voices` at the first sound (7.7
  s), the voices starting a little apart, as the MMX and float runs do
  ([bnk.md](../formats/nfs/bnk.md)); its level is 0.02 dB lower.

  Placement, surround and slow frames were checked on 200-second runs
  into a race (`nfs-host -native -wav`):
  - **Placement.** iSND pans and places sounds itself, inside the mix it
    writes, and nothing on the way to the speakers mixes the two channels
    together: the mixer gives both the same gain unless the stream is
    panned. How wide the mix is depends on what is playing. Measured
    with `-wav` and `-voices` in a stereo race (`70000:+up`, the
    accelerator held, 240 seconds):
    - Driving, the side channel (left minus right) is 12.6 dB below the
      middle (10 to 14.6 in 10-second spans), and left and right are
      within 1 dB of each other. The mixer's own sums agree (-12.6 dB).
    - Sitting at the start line, before the accelerator, the side is 1.8
      dB above the middle: left and right are slightly opposite
      (correlation -0.2). The mixer's sums say the same, from voices
      whose levels all have the same sign, so it's in a stereo sound
      itself.
    - An earlier run found the race near 0 dB (-1.4). Its keys tapped
      the accelerator for 100 ms, so the car never left the line: that
      was the second case. The 2 dB louder right channel noted then
      isn't in the driving race.
  - **The audio mode** is one byte at 0xc0 of
    `FeData/config/config.dat` (0x6fbc00 once loaded): 0 stereo, 1 mono,
    2 Dolby, 3 D3D/EAX. Found by stopping at 0x40fb44 (`-watch`), where
    the sound's start-up reads it: with byte 0xc0 set to 3 in the save
    folder's copy the register holds 3, with byte 0xac set to 3 it holds 0.
    The mixer tests the same address for Dolby (`cmp [0x6fbc00],2` at
    0x417299 and 0x418119). Byte 0xac (0x6fbbec) is another setting: the
    code at 0x41057f picks 1 to 7 at random when it's 0.
  - **Dolby Surround** (audio mode 2) works: the game makes it itself,
    in its own mixer, and nothing in Reluva is in the way. Worked out
    from the code and from two races (modes 0 and 2, `-native
    -deterministic`, byte 0xc0 set):
    - The device is opened as for stereo: start-up turns mode 2 into 0
      (0x40fb44). Dolby is only in how voices are placed.
    - In mode 2 the game starts its sounds with voice type 1 (`mov
      [ebp-0x9],ah` with ah 1 at 0x418467 and 0x41854b, `[ebp-0x5]` at
      0x41823e) and an angle round the listener. Each update then sets that angle
      (0x4e9850, called at 0x4172ad and 0x418135) where stereo sets a pan
      (0x4e9950). In stereo, angles behind are folded to the front
      (`0xbfff - angle`, 0x417309).
    - The angle becomes left and right levels at 0x52b1c0. For other
      voice types both are made positive (0x5224b0). For type 1, an
      angle from 0x4000 to 0xc000 (behind) has one level negated
      (0x522320; 0x522481 left, 0x522491 right): the sound goes into
      left minus right, as a Pro Logic decoder's surround.
    - The levels are signed bytes (0x5006a4). The mixer's set-up turns
      them into float gains with `fild` (0x520a71), so a negative one
      mixes in opposite phase; the native mixers keep the sign
      (`nfs-native-check` tries gains from -1.5 to 1.5, all the same).
    - Counted with `-watch` over a 180-second run: in stereo none of
      843 level settings (0x5006a4) has a negative side, and of 939 at
      the mixer (0x520a8e) none. In Dolby, 505 of 2913 have opposite
      signs, the rear branches run 114 and 388 times, and 1554 of 4041
      mixer settings are opposite (the lowest level -19 of 127).
    - **How loud.** Measured on two races with the same keys (modes 0
      and 2, `-deterministic 50000`, `70000:+up`, 240 seconds, from 72
      seconds on), with `-voices` and `-wav` lined up by the game's time:
      - 8,439 of 216,724 mixer calls in Dolby have opposite signs (none
        in stereo). Voices behind play in 244 of 1,681 windows of 100 ms.
      - Over the race, the voices behind have 18.4 dB less energy than
        those in front. Their own L-R is 5.3 dB above their L+R (their
        two levels differ, so some stays in L+R).
      - Their L-R is 19.3 dB below the front's L+R, and 6.6 dB below the
        front's own L-R: the stereo spread fills L-R more than they do.
        That adds up to the recordings: S/C -12.6 dB in stereo, -11.8 in
        Dolby.
      - When the voices behind are loud, Dolby shows. Window by window,
        at the same game moments: where they come within 10 dB of the
        front (58 windows), S/C is -10.3 dB in stereo and -5.7 in Dolby;
        within 10 to 20 dB (69 windows), -11.3 and -9.8; elsewhere the two
        agree within 0.2 dB. In 3 windows they are louder than the front,
        and there S/C reaches 0 dB (-3.8 in stereo).
      - So a Pro Logic decoder would hardly steer to the rear. It steers
        to the surround when L-R outweighs L+R, and in this race it never
        clearly does. The sounds behind reach the surround speakers
        through the matrix, about 19 dB under the front over the race,
        and 5 to 10 dB under it at their loudest moments.
    - **Which voices are behind** (October 2026). Logged with `-watch`
      at the two angle updates, 0x4172ad (esi the sound slot, edx the
      angle; 0x417080) and 0x41812b (ebx the slot, edi the angle;
      0x417fd0), over a Dolby race (`-deterministic 50000`,
      `70000:+up`, 240 seconds, single race, no cops, no wall hits):
      7,713 updates, 1,465 of them behind (0x4000-0xc000); the rear
      branches ran 778 (0x522481) and 1,107 (0x522491) times.
      - **Slots 4-7, the other cars' engines**: 1,805 updates, 773
        behind (43%), so 53% of all the rear placements. They are the
        four cars heard, kept at 0x570b78 (0x38 bytes each, cleared at
        0x411719-0x41173a), each played through 0x4179d0 with slot =
        its index + 4 (0x412013, from 0x411e60), its angle from the
        listener (0x410d30).
      - **Slot 26 (0x1a), the player's own car**: 3,117 updates, 108
        behind (3%, 7% of the rear ones). 0x415210, the player car's
        sounds, plays the road-surface and track-zone loops there
        (0x41542b; 0x415050 by slice ranges, table 0x552d54); slot 0x24
        is the second player's in split screen, whose angles are fixed
        left and right (0x3fff, 0xc000).
      - **Slot 21 (0x15), a moving object's sound**: 917 updates, 437
        behind (48%, 30% of the rear ones). `0x410e30` keeps the two
        nearest of the track's moving objects (the `.col` file's block 7,
        type 3) and plays them on slots 20 and 21 (`0x4113d4`: n + 0x14;
        0x22 + n for the second player), from a per-track table in the
        exe (`0x552744`, 18-byte rows: s16 range, s16 key offset, s16
        sound, then key ranges and two more sounds, -1 none). Slots
        18-21 take the track's bank, table entry 3 (`0x417080`; loaded
        at `0x414425`: `tr3dNN`, or `tramNN` without 3D sound, with
        `n`, `w`, `nw`; `trambNN` from track 9). Hometown's rows give
        sounds 31, 6 and 7, but Hometown's `.col` has one object, so
        only row 0 is used: slot 21 is **the loop flown 52 m up**, sound
        31 (November 2026). A lone object always lands on slot 21: the
        swap that keeps the two nearest on their slots
        (`0x411047`-`0x411094`) starts from 0, 0 and then -1. The rows,
        their fades and the special sounds (-2, -3 on slot 19) are in
        [bnk.md](../formats/nfs/bnk.md) (The moving objects' sounds).
        Slots 18 and 19 are the `.frd` placed sounds (`0x41d210`).
        Both kinds of special play in runs where the computer drives
        the player's car (`sn-m1`, `sn-a1`; bnk.md). `-watchread ADDR`
        or `-watchread reg+off` (as many as wanted) prints those dwords
        at each `-watch` stop.
      - **Slots 22-25 (0x16-0x19), the other cars' skids**: 201
        updates, 147 behind. Each car's sound list (car +0x7DC, up to 8
        of 0x18 bytes, `0x4284e0`) gets category 6 from `0x429f10`:
        slot 8 for wheels 0-1, slot 10 for wheels 2-3, the material
        from the skid's surface and weather (car +0x990 + wheel, table
        `0x55309c`, `0x429d20`). The player's own play on slots 8-11;
        for one of the four cars heard, `0x411e60` plays any entry with
        a slot on its index + 0x16 (`0x41205b`). `0x414d70` turns the
        material into a `Gen.bnk` slot (1 → 42, 2 → 47, 3 → 45, 4 →
        46, 7 → 41, 14 → 43, else 44), and slots 22-25 use bank entry
        2, `Gen.bnk` (table `0x552798`, one byte per slot).
      - Slots 8 and 10, the player's own skids (above): 1,673 updates,
        none behind.
      - Not heard in this race: the cops' sirens (slots 0x1b-0x1e,
        0x417fd0 via 0x411c50, for a car with +0x200 bit 0x20 and
        +0x710 bit 0x10), the traffic (slots 0-3, 0x411cb0, +0x200 bit
        4) and the one-shots on slot 0x1f (wall hits, 0x416d90).
        The sirens are heard in footage of the original
        ([bnk.md](../formats/nfs/bnk.md), Seen in footage).
      So the rear channel carries mostly the other cars' engines, as
      they fall behind; the player's own car is almost always in front.
    - Before `-wav` followed the game's clock, the recordings couldn't
      be lined up at all. Even now they can't be subtracted: they are the
      same, sample for sample, for the first 41 seconds, and then the
      sounds' phases drift apart (no correlation at any lag up to 0.1
      s), though their levels match window by window.
    - **Decoding it** (November 2026). `common/audio/prologic.cpp` is a
      model of a Pro Logic decoder. It is written from the public
      description of the matrix, not from any decoder's code:
      - The passive matrix: left = Lt, right = Rt, centre = (Lt + Rt) /
        sqrt 2, surround = (Lt - Rt) / sqrt 2.
      - Active steering: the power of Lt, Rt, Lt+Rt and Lt-Rt, smoothed,
        gives the strongest sound's direction and how much it dominates.
        The time constant is 200 ms, and 10 ms while one direction
        clearly dominates. The decoder takes that sound out of the
        speakers it leaks into and pans it between the two speakers next
        to its direction. The rest stays passive.
      - The surround is low-passed at 7 kHz and delayed 20 ms.
      - Left out: the surround's modified Dolby B noise reduction. The
        game never applies it (its mixer puts sounds behind straight into
        L-R), so the decoder's half would only dull the surround. Also left
        out: auto-balance and the centre modes.
      - `reluva-tool prologic in.wav prefix` writes the four channels as
        mono WAVs and prints levels window by window (`-window ms`, `-csv`,
        `-passive`). Unit tests: `tests/test_prologic.cpp`.
    - **What the decoder makes of a race.** The same two races as above
      (modes 2 and 0, `-deterministic 50000`, `70000:+up`, 240 seconds;
      the `-voices` log gave the same counts again), from 72 seconds on,
      in 100 ms windows:
      - The steering sits on the centre almost all the time (direction 44
        degrees, gain 1.0): the player's engine dominates. It never
        steers to the surround in the race, in either mode.
      - The surround is 12.7 dB under the front in Dolby (12.2 passive)
        and 13.4 dB in stereo (12.9 passive).
      - Where the voices behind come within 10 dB of the front (61
        windows), the surround rises to 8.0 dB under the front (7.0
        passive). The steering weakens (0.73) but stays near the centre.
      - At the loudest moment behind (211.2 s, the voices behind 3.3 dB
        over the front), the passive surround is 2.4 dB under the front;
        the active one 12.8 dB under it. The steering, held on the
        centre, takes the sound out of the surround.
      - Even alone, a voice behind wouldn't go to the surround speakers
        clearly. Its two levels differ (its L-R is 5.3 dB above its
        L+R), which puts it at about 163 degrees on the matrix's circle:
        between the surround (135) and the side (180), nearer the side.
        The model pans such a sound mostly to left or right, the
        surround about 4 dB lower (worked out, not measured).
      - The only steering to the surround is before the start (50 to 70
        s), in both modes alike: there the game's stereo sound has more
        L-R than L+R (Placement, above). That is not Dolby.
      - So in this race Pro Logic adds little. The rear sounds are heard
        through the matrix, mostly at the passive level.
    - **Playing it.** `surround=` in the game's section of `reluva.ini`
      sends the NFS engine's whole output through the decoder (the game,
      and the pages' sounds and music too):
      - `prologic`: active, to a 5.1 device (centre on its own, LFE
        silent) or a 4.0 one (centre folded into left and right at -3 dB),
        the surround to both rear speakers at -3 dB. When the default
        device has fewer than 4 channels, the sound stays stereo, as mixed.
      - `passive`: the same with the passive matrix alone.
      - `headphones`: decoded and folded back to stereo: the centre to
        both ears at -3 dB, the delayed surround to both at -6 dB.
      - Without it the output is unchanged. `System::audioDeviceChannels`
        and `startAudioChannels` (SDL: `SDL_GetAudioDeviceFormat`, a
        stream of 4 or 6 channels) open the device. Checked with SDL's
        disk driver at 6, 4 and 2 channels. `nfs-host` has no device: its
        `-wav` recording goes through `reluva-tool prologic` instead.
  - **D3D/EAX** (audio mode 3). Worked out from `eacsnd.dll`'s code and
    a race with the mode set:
    - The game asks for 3D only if the sound card has 3D buffers in
      hardware. `iSNDdirectcaps` (0x401428 in the DLL) sets its 3D bit
      (0x20000) when DSCAPS' most hardware 3D buffers (+0x2c) isn't 0. The
      game then starts the sound with 0x20808 in place of 0x808 (0x40fbd5).
      The stand-in's `GetCaps` says 64 hardware buffers, 3D ones too
      (the free counts drop as they're used). Before, it said none, and
      mode 3 played as stereo.
    - With the bit, `iSNDdirectstart` makes the primary buffer with
      `DSBCAPS_CTRL3D` (flags 0x11) and asks it for
      `IDirectSound3DListener` (0x401fc4). Without it, the start fails
      ("NO 3D LISTENER INTERFACE"). The listener is never used after:
      its position, orientation and factors stay the defaults.
    - The mix is still the game's own, into a streamed buffer. Some
      sounds go to buffers of their own: `iSNDdirectcreate3dbuf` makes
      one per sound, mono, flags 0xb4 (`LOCHARDWARE`, `CTRL3D`,
      `CTRLFREQUENCY`, `CTRLVOLUME`), filled once through the game's
      callback (0x40117c). `iSNDdirectplay3d` (0x402814) plays one,
      through `DuplicateSoundBuffer` if it's playing already; gets its
      `IDirectSound3DBuffer`; sets its frequency, position and volume;
      and plays it.
    - `iSNDdirectpos3d` (0x402668) places it: the game's callback gives a
      vector, scaled by 1/65536, to `SetPosition`, applied at once. In the
      race the vectors are all one unit long, so DirectSound's distance
      model never comes in. The game sets the loudness itself, with
      `SetVolume`, and only the direction is DirectSound's: y is 0, and x
      and z go round as the car turns.
    - No EAX. Neither `eacsnd.dll` nor `nfs3.exe` holds the GUIDs of
      `IKsPropertySet` or EAX's property sets, so nothing asks for them.
      EAX cards of the time added their reverb to DirectSound 3D's buffers
      by themselves. The stand-in answers no to `IKsPropertySet`, as a
      card without EAX does.

    The stand-in (`host/sound3d.h`, `directx.cpp`) has both 3D
    interfaces, with deferred settings and `CommitDeferredSettings`, and
    `DuplicateSoundBuffer` (the memory shared, freed with the last
    buffer). It mixes a 3D buffer in software, by DirectSound's model as
    Microsoft documents it: full volume up to the least distance, then
    min / (min + rolloff * (d - min)), no quieter past the most distance;
    the cone's outside volume; Doppler from the velocities along the
    line. For the pan, the nearer speaker gets it all and the farther one
    is turned down, by up to 20 dB for a sound straight to one side. Real
    cards used HRTFs or speaker panning of their own, which aren't
    documented. Behind the listener, Reluva turns a sound down by up to
    3 dB and muffles it, through a one-pole low-pass whose cut-off falls
    from 20 kHz beside to 4 kHz straight behind (`rearGain`,
    `rearCutoff`), as a head shadows sound from behind; that rule is
    Reluva's own, as DirectSound leaves it to the card. Doppler changes
    the pitch heard, not the play cursor the game sees; the game gives
    its sounds no velocities, so it never comes in.

    Checked on a race from a save folder with byte 0xc0 set to 3
    (`nfs-host -native -wav`, 5 billion instructions): the game makes 4
    3D buffers and plays them 9 times (3 through duplicates), with 2,070
    `SetPosition` calls, and no call fails or isn't stood in for. Over
    100 seconds of 3D buffers played, their samples' RMS is 9,332 and
    what reaches each speaker 470 or so: the game plays them quietly. In
    the race (the last 60 seconds) the mix's level, in 10-second spans,
    is within 0.5 dB of the stereo race's; the spans around the loading
    pause before it differ by up to 2.3 dB, as the two runs reach it at
    slightly different times.
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
- **d3da (the Direct3D renderer)** isn't run, and needn't be. Running
  it would mean standing in for Direct3D: devices, textures as surfaces,
  a Z-buffer and DrawPrimitive. Drawing THRASH's calls on the GPU
  directly, from the same calls the native rasterizer takes, gets the
  same pictures with no Direct3D at all. The game can still be told it
  has d3da, and then takes its Direct3D path (The Direct3D look, below).
  (An earlier note here had the game pick d3da only with a "3D Device
  Description" in the registry. Read again in November 2026, it picks
  its renderer by the "Thrash Driver" name alone, below; the description
  is only searched for two cards' names.)

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

- **Sky and fog.** The game drew neither: no horizon and no clouds
  behind the scenery, and no haze in the distance. Two causes, found in
  `nfs3.exe` and fixed in the host:
  - **The detail levels.** On a first run (no `config.dat`) the game
    times the processor and sets its detail levels by its speed
    (0x472d77): above 266 MHz the highest, then 233, 200 and 166 MHz;
    halved for a software renderer. One of them turns the horizon on
    (0x6fbc20, 0x6fbc24). It times RDTSC against its clock, and RDTSC
    counted instructions, so it found a 60 MHz processor and turned the
    horizon off. RDTSC now reads as a 300 MHz processor's would by the
    game's clock (`Cpu::timestampCounter`). The game then picks the
    highest levels and draws its sky dome, horizon picture and clouds.
    The levels are in the game's Options; a saved config can still have
    the horizon off.
  - **Fog is for 3dfx cards only.** The game sends its fog states only
    with `voodooa.dll` or `voodoo2a.dll`; d3da and softtria get none
    ([thrash.md](../formats/nfs/thrash.md), Fog). So the Play page tells
    the game it has a Voodoo 2: the registry names `voodoo2a.dll`, and
    when the game loads it, softtria is loaded in its place (it keeps
    THRASH's books; the drawing is Reluva's). Told of a 3D card, the
    game also uses its 16-bit texture sets rather than `_8.qfs`. Fog is
    drawn as `voodoo2a.dll` has Glide draw it, natively and on the GPU.
    `nfs-host` does the same; `-present none` or `-renderer` gives the
    game softtria as before.

  Checked with a copy of the game whose `.hrz` files give red fog
  (density 100, then 60 on every track): the far road and scenery turn
  red while the dashboard and HUD don't, natively and on the GPU (Play
  page under Xvfb on software Vulkan). With the real files, Hometown
  has its sky and clouds and a light haze in the distance.

- **The Direct3D look** (November 2026). `renderer=direct3d` in the
  game's section of `reluva.ini` (the Play page) or `nfs-host
  -direct3d` tells the game its renderer is `d3da.dll`, and the game's
  own code takes its Direct3D path; softtria is loaded in its place, the
  drawing is Reluva's, and it follows d3da's device set-up: bilinear
  filtering, no dithering ([thrash.md](../formats/nfs/thrash.md), d3da's
  device defaults). The default (`voodoo2a.dll`, fog) and
  `renderer=glide` are unchanged. Why this way: the look is then the
  game's, not Reluva's imitation of it, and no Direct3D is needed.

  How the game picks its renderer (read from `nfs3.exe`): `0x4b6020`
  sets the renderer number `0x7a3a54` from `0x4b5720`, which reads the
  registry's "Thrash Driver" (`0x4b6400`) and finds it in the table at
  `0x55fd68` (0 `nulla`, 1 `softtria`, 2 `softlina`, 3 `voodooa`, 4
  `voodoo2a`, 5 `ragea`, 6 `d3da`, 7 and 8 `powervra`; command-line
  flags can override it), then the renderer byte `0x7a3a58` = 1 << that
  number: `d3da` is bit 0x40. "D3D Device" (a number) goes to
  `selectdisplay`; "3D Device Description" (`0x4b64c0`, into `0x7a3a74`)
  is not needed, and is only searched for two cards' names. The host
  gives "Direct3D HAL", neither of them.

  What the game does differently with bit 0x40, from every reader of
  `0x7a3a58`:
  - **Set-up** (`0x4b59c0`, case 6): it checks the desktop has at least
    8 bits a pixel; `selectdisplay`, `setvideomode(1, 2, 1)`; five
    `setstate` probes (`0x4b5940`: 0xd 1, 0xd 0, 4 2, 0xce 1, 0x68 1)
    into `0x7a3a5c`, which nothing reads; states 0xa 2, 0x68 0, 4 1, 6
    1, 2 0; the z-buffer flags (`0x7a3a68`, `0x7a3a60`) as for the
    Voodoos; fog mode 0, so no fog states are sent (thrash.md, Fog); a
    texture budget (`0x55fed0`) of 2 MB against the Voodoos' 8, which
    only limits the resolutions offered (`0x4bed90`); and `0x7a3a70` 0,
    so `0x4bc680` sets the FPU's control word again (Direct3D lowers its
    precision). The texture memory warning (`0x4b5ea0`) adds `about()`'s
    +0x74 to +0x70.
  - **Drawing**: the clouds' constant alpha (`0x47ae10`, by mask
    0x1e0), the dimming by the effects intensity (`0x41a120`,
    `0x41b9b0`, `0x47b850`, `0x491190`, `0x491bc0`, `0x492370`,
    `0x492980`, `0x49d800`, `0x4cb750`, `0x4cbc90`, `0x4ddb40`,
    `0x4de070`, `0x4de700`), particle kind 7's colours (`0x4cb670`), its
    own polygon drawers (`0x434870`), text drawn point-sampled when
    scaled and bilinear after (`0x4d7160`, state 7)
    ([hrz.md](../formats/nfs/hrz.md), The Direct3D renderer).
  - **Menus**: the options' SFX Intensity slider is offered only under
    Direct3D (`0x448610`, `0x44d2a0`; hidden otherwise); the "license"
    widget's two gradient bands are left out (`0x442450`, also for
    softtria); "texteffects" (`0x4a4410`) is left out under Direct3D only
    for a device described as a RAGE PRO (and always for softtria).
  - **Defaults**: on a first run the effects intensity is 1, or 0.5 for
    an "STB Velocity 128" (`0x472d10`). The config keeps the renderer
    byte (`0x6fbc10`); when it differs from the one now in use the game
    sets its detail defaults again (`0x472980`, `0x4730d0`), so changing
    between `renderer=direct3d` and the default resets the graphics
    options, as changing the card in 3D Setup would.
  - **The dashboard**: loaded when `about()` reports texture memory
    enough (`0x488a80`); softtria's flag (+0x75 bit 0x80) loads it
    either way.

  Checked on Hometown in the weather, `nfs-host` frame 2800 (in the
  car at the start): no haze, the far trees and fields plain, the sky's
  colour and spread as `Rain` 0:10's, textures smooth and undithered;
  the Summit in the weather without its white haze, dark hills and grey
  clouds as in `Snow` 0:40, though the clip's sky is brighter
  ([hrz.md](../formats/nfs/hrz.md), The Direct3D renderer, In the game);
  and the Play page in the app (Xvfb, software Vulkan) through the
  intro to the main menu.

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
  and a pad's rumble. A real gamepad was tried by the user (November
  2026): driving and force feedback worked. A real wheel hasn't been
  tried yet, so how the forces feel on one is untested.
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
  instructions a second and a frame, with the shares of the game's main
  thread, its timer thread and its other threads; whether the GPU or
  software draws them, and how
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
- **MMX** (`nfs-host -mmx`, November 2026). CPUID then reports MMX
  (`Cpu::reportMmx`; off by default, as on the Play page), and the
  game takes its MMX paths: the integer sound mixer, the MMX EA-XA
  decoders and an 8-byte copy ([bnk.md](../formats/nfs/bnk.md), Run on
  the interpreter, lists them). The float mixers, which run natively,
  are then not used. On the race above (5 billion instructions,
  `-deterministic 50000`) the game ran without a fault and the same each
  time. It made 4,244 frames against 4,298 without MMX (1.3% fewer),
  its main thread doing 95% of the work against 97%: the MMX mixer runs
  in the interpreter. The interpreter's own speed was unchanged (125
  seconds against 128 for the run). The sound is the same in level and
  spectrum; it is not sample for sample the same (bnk.md says why).
  `-count ADDR` (repeatable) prints how many times the instruction at
  ADDR ran, as the routines' counts there were taken.
  `-drive CAR:TARGET:FROMMS[:TOMS[:GAP]]` steers a human's car (its
  record's address) at another car from FROMMS to TOMS: it writes the
  steering and pedals where the game unpacks the controls (`0x4314c9`),
  at full gas or holding GAP metres behind the target at its speed, for
  scripted chases in either player's car (`-drivesign -1` turns the
  steering's sign round). It was made for the Hot Pursuit runs in
  [physics.md](../formats/nfs/physics.md).
  `reluva-tool nfs-mmx-check <game> [trials]` runs the two mixers on the
  same voices: the game twice in the interpreter, the MMX flag clear in
  one and set in the other, each voice started with `iSNDmixstart` and
  mixed with the game's own routines. It prints, for real and random
  voices, how the MMX output differs from the float one sample by
  sample (the mean and RMS, with and without the 127/128 gain, the
  share of even samples, clipping), and how often each MMX routine ran.
  It also runs the 8-bit routines, which no retail sound uses. The
  results are in bnk.md (Run side by side).
- **Memory written at a moment** (`nfs-host -poke WHEN:ADDR:VALUE[:SIZE]`,
  as many as wanted). It puts a game state in place without the menus.
  WHEN is `ms=N`, once the game's clock reaches N (checked at each thread
  change, every 20,000 instructions), or `at=ADDR`, just before the
  instruction at ADDR runs; `at=ADDR#N` waits for its Nth time. VALUE is
  a number of SIZE bytes (1, 2 or 4; 4 if not given) or `bytes=` and hex
  in memory order (`bytes=0a0b0c`). A poke is applied once. `-pokeevery`
  takes the same text and applies it each time: an `at=` poke each time
  the instruction comes, a `ms=` poke at each thread change from then on
  (it holds the value). Each poke applied is logged with the old bytes
  (an `-pokeevery` only its first three times).
  `-dumpmem WHEN:ADDR:SIZE:file` saves SIZE bytes from ADDR at such a
  moment (`-dump` saves all of memory, at the end). An `at=` moment traces
  each instruction, as `-watch` does, so the run is slower.
  - **A Hot Pursuit as the police.** With the clicks of a single race,
    two pokes before the race's set-up picks its kind (`0x474ca0`) make
    it a Hot Pursuit with the player in the Pursuit Corvette:
    `-poke at=0x474ca0:0x6fd3b8:3 -poke at=0x474ca0:0x6fd2bc:12`. Poked
    later, at `0x473bf0`, the single race's set-up is already chosen: the
    race looks like a pursuit, but the player is a racer.
    `0x6fd2bc` is player 1's car (player 2's is `0x6fd328`), the number
    the car screen steps through (`0x43ac61`) and the set-up copies into
    the car's slot (`0x47373d`). The cars are numbered 0-15: 12, 13 and 14
    are the Pursuit Corvette, Diablo SV and El Niño, the cars whose flags
    (car record +0x380) have 0x10 (tested by `0x4396b0`). The race shows
    the police car, its radio and "tickets: 0/0" against 7 racers.
  - **The spike strip.** The player's car (record `0x5e1120`) runs the
    police states at `0x405740` (state at +0x6E0, table `0x405714`). The
    strip is laid only in states 3 and 4. As a cop the player starts in
    state 3; the chase (4) starts when the siren is on (key H) and a
    racer within 50 slices is over the speed limit by 2 m/s
    ([physics.md](../formats/nfs/physics.md)). First seen with the late
    pokes above, where the player was a racer (state 2) and had to be put
    in state 3 by hand (`-poke ms=62000:0x5e1800:3`): with S pressed
    every 2 s, the first press lays a strip (`0x4c9dd0`), the second
    finds it still out, the third clears it (`0x4c9f50`). A frame in the
    chase view shows the spikes across the road.

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
    - **Messages** ([net.md](../formats/nfs/net.md)). Each one starts
      with a big-endian 16-bit length of what follows: the receiver peeks
      at 2 bytes, then at that many more, then reads them. (Taken at
      first to include itself; the logs of `-netlog` showed otherwise.)
    - **In a race.** The host sends a 35-byte frame of every car's
      controls about 30 times a second, and each player its own controls
      in 8 bytes. Nothing used UDP.
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
  - **IPX, over UDP** (October 2026). The game's IPX play
    (`win\ipx.c`) uses Winsock's `AF_IPX` datagram sockets. The
    stand-in carries them over UDP: an IPX node is an IPv4 address (its
    first four bytes), an IPX socket number a UDP port, and the broadcast
    node (all FF) a UDP broadcast (to `-broadcast`'s addresses if given).
    `getsockopt` reports one adapter and its address (`IPX_MAX_ADAPTER_NUM`,
    `IPX_ADDRESS`); the sockets may broadcast without asking. Two copies
    (127.0.0.2 and 127.0.0.3): A chose Network (IPX), Create Game, a name
    and Peer-to-Peer; B chose Refresh Game List, the game and Join. Both
    reached the lobby listing both players, and raced Hometown, about
    2,000 race messages each way, until B's run ended. The game finds
    games on IPX socket 0x452 and then talks through EA's reliable layer
    ([net.md](../formats/nfs/net.md)).
  - **Serial, over TCP.** COM1-COM9 are one link over TCP
    (`-serial listen:port` on one copy, `-serial host:port` on the
    other; `host/serial.cpp`), as through a null-modem cable: what one
    writes the other reads; the connecting copy tries again every half
    second until the other listens. The comm functions keep the line
    settings and report the carrier while the other end is there. The
    game's serial driver reads overlapped from a thread of its own
    while holding a lock, so an overlapped read with nothing waiting is
    left pending (ERROR_IO_PENDING), its event signalled when bytes
    come, and `GetOverlappedResult` takes them (`Win32Process`). The
    game first looks for a modem on the line (`+++`, `ATE1`, `AT`), then
    talks through the same reliable layer as IPX's data packets
    ([net.md](../formats/nfs/net.md)). Two copies, each choosing Serial
    and Connect, reached the lobby (the game picked one as host), and
    raced Hometown, eight cars on both screens, their race clocks a
    second apart, 8,337 race messages each way.
  - **Modem, over TCP.** With `-modem`, LoadLibrary("tapi32.dll") finds
    a TAPI 1.4 stand-in (`host/serial.cpp`): one line device, a data
    modem, whose calls are TCP links like the serial port's. The copy
    that answers listens (`-modem listen:port`); dialing connects to the
    number typed if it is an address (`127*0*0*1:2400`, * for the dots
    as on a keypad) or else to `-modem host:port`. `lineGetID` gives a
    handle to the modem's port ("comm/datamodem"), which the game's
    serial driver takes. Things the game counts on, each found by it
    hanging:
    - TAPI's events reach the thread that started TAPI when it next asks
      for messages or sleeps; its GetMessage wakes for them, and comes
      round every 100 ms while a call is under way, as TAPI's window gets
      status messages;
    - the line's port is there before a call (`lineGetID` on the line);
    - a call connects two seconds after the dial or the answer: the
      game's main thread waits for its TAPI thread to start before it
      waits for the call, and missed one connected at once.

    Two copies (127.0.0.4 answering, 127.0.0.5 dialing "12") reached
    the lobby, both players listed, the answering copy the host, and
    raced Hometown, eight cars on both screens, about 16,900 race
    messages each way (the serial link's 12-byte packets). Their last
    pictures' race clocks read 11 seconds apart, but the pictures weren't
    taken together: each copy saved every 200th frame, and B, drawing
    more frames, saved its last 10.4 s before A's (by the `-framelog`
    times). Two pictures taken 49 ms apart read 3:03.96 and 3:03.85: the
    clocks agree. Both copies sent their first race packet within 15 ms
    of each other and their last within 50 ms, 271 s later.
  - **`-netlog file`** writes each message sent and received: the game's
    milliseconds, the direction, the socket, the peer and the bytes.
  - **`-netdrop N[:from[-to]]`** loses every Nth packet a copy sends
    (from game millisecond `from` until `to`), to see the game's resends:
    an IPX datagram or a serial or modem write goes nowhere, and a TCP
    `send` fails with WSAEWOULDBLOCK, as with a full buffer. `-netlog`
    writes each with `x` for its direction. What it showed is in
    [net.md](../formats/nfs/net.md) (Timers and window). For holding the
    driving keys in a race, `-keys` knows the keypad's `kp0`-`kp9`.
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
  (4.9 seconds), and a track 72 million (16% fewer).
  - Later (October 2026), with the interpreter's helpers inlined
    ([x86.md](x86.md)), start-up to the first menu frame went from
    7.0-7.6 s to 6.5-6.6 s here, in real time with `-nomovie`. The faster
    build ran more instructions in that time (670 million rather than
    550): part of start-up is the game waiting on its clock, polling its
    timer service. The title movie, if it plays, spends 62% of its
    instructions in one loop, the YUV-to-RGB conversion at `0x4fd61c`
    (lookup tables at `0x9f4fb0`-`0x9f6218`). The track's wait
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
- **Not planned:** running d3da itself (see above; its look is the
  game's own, The Direct3D look; a shared Direct3D stand-in is planned
  for later, when other engines need one, below). The Glide renderer isn't needed to
  play (the native and GPU drawing do that, the game told it has a Voodoo
  2 to draw as for one, see Sky and fog), but since November 2026 it runs:
  the retail `voodoo2a.dll` on Reluva's Glide stand-in (`-glide`, below,
  Glide stand-in), as a reference for the native drawing.

### Phase 5A: native game (if not)

1. Front end from the `.mnu` files and `text.*`.
2. Single race.
3. Physics from `carp.txt` ([physics.md](../formats/nfs/physics.md)).
4. AI and traffic from the speed tables and the virtual road.
5. Pursuit.
6. Replays.

Each part is checked against recordings of the original.

### Glide stand-in (Glide 2 and 3, software and GPU, November 2026)

A shared, clean-room stand-in for 3dfx's Glide library, written from
3dfx's public documents (the Glide 2.4 and 3.0 Reference Manuals and
Programming Guides, the Voodoo2 and Voodoo3 specifications) and the
interface values in its public SDK headers, never from Glide's own
sources or other wrappers and emulators ([glide.md](glide.md)). It is in
`src/common/glide/` for any engine: a version-neutral Voodoo pipeline (the
16-bit buffers and their dither, texture memory and formats, the combine
units, fog table, chroma-key, alpha test, z and w buffers, the linear
frame buffer, gamma) drawn by a software reference rasterizer or by a GPU
backend that follows it pixel for pixel; Glide 2 and Glide 3 front ends
over it; and bindings that serve them as `glide2x.dll` and `glide3x.dll`
to the x86 interpreter.

Done for NFS III: the retail `voodoo2a.dll` (1998-07-31) runs on it
unchanged, through the front end and races. All 59 Glide functions it
looks up are carried out (glide.md, NFS III's voodoo2a.dll).
- `reluva-tool nfs-host -glide` runs the game so: the registry names
  `voodoo2a.dll`, which is loaded as it is and finds `glide2x.dll` served
  by the host; each `grBufferSwap` is a frame.
- `-glidecompare out.csv` also draws each THRASH call natively beside it
  and compares the two frames of every page flip
  ([thrash.md](../formats/nfs/thrash.md), The Glide stand-in).
- The Play page draws so with `renderer=glide` in the game's section of
  `reluva.ini` (the native THRASH and its GPU drawing stay the default).
  Glide's pixels are drawn on the GPU when there is one (`glide=software`
  for the software reference).
- `-glidegpu` draws on the GPU in nfs-host too, and `-glidegpucompare
  out.csv` draws every call both ways and writes each frame's difference
  ([thrash.md](../formats/nfs/thrash.md), The Glide stand-in).
- The CD's Voodoo3 patch DLL (1999-04-21, Glide 3) runs on the Glide 3
  stand-in, on a Voodoo3: `-glidedll <file>`, or `glidedll=<file>` with
  `renderer=glide` on the Play page; the file stays outside the repo and
  the install. It goes through the menus into races, and draws the game
  without fog, as its code says (thrash.md, The Voodoo3 patch's
  voodoo2a.dll). The version check the game makes passes: the DLL takes
  the version the game sends it before `about()`.
- The install's Voodoo Graphics module `voodooa.dll` (Glide 2) runs on a
  Voodoo Graphics stand-in: `-glidedll <install>/voodooa.dll -glideboard
  voodoo1` (or `glideboard=voodoo1` beside `glidedll=` on the Play page).
  With the board's 2 MB of texture memory the game leaves the cockpit out;
  `-glideboard voodoo1,tmu=4` (the 6 MB card) has it, and the frames are
  then within a fraction of a level of the Voodoo2's
  ([thrash.md](../formats/nfs/thrash.md), The Voodoo Graphics module).
  `-glideboard` takes `voodoo1`, `rush`, `voodoo2`, `banshee`, `voodoo3`,
  with `,tmu=MB`, `,fb=MB`, `,tmus=N`, `,tmurev=N` (glide.md, Boards).

Next: the open rounding questions (glide.md, Open questions); the
undocumented dither rounding still needs a real card's frames.

### Later: a Direct3D stand-in (planned, not started; when more engines need it)

NFS III doesn't need one: its Direct3D look comes from the game's own
code, told it has `d3da.dll` (The Direct3D look, above). Other engines
Reluva hosts may draw only through Direct3D, and then a stand-in is
wanted, shared like the Glide one:

- **Clean room.** Written from Microsoft's public DirectX documentation
  (the SDK reference for each version's interfaces, render states and
  capability bits) and reverse engineering of each game's own calls,
  never from other wrappers' or emulators' sources.
- **Scope.** DrawPrimitive-era Direct3D 5 to 7, fixed-function, first
  (devices, viewports, textures as surfaces, render states, texture
  stages, the Z-buffer), with room for Direct3D 3's execute buffers and
  for Direct3D 8 and 9 later.
- **Shape.** COM objects served to the x86 interpreter as the Glide
  stand-in's functions are (`IDirect3D`*n*, devices, textures), over a
  version-neutral pipeline with a software reference rasterizer, and a
  GPU backend later.
- **Card profiles, optional.** Profiles imitating typical cards of the
  time (16-bit dithering, fog missing or vertex-only, simpler or no
  filtering, the capability bits they reported), each labelled an
  approximation.

Direct3D had no single original look: each card and driver drew
differently, and games adapted to the capabilities reported. So the
stand-in's default shows what the game asked for, drawn on an idealised
device, and the profiles are guesses at particular cards, not
references.

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

**Tools for reading the executable** (November 2026): `tools/nfsre/`
holds query scripts over your own copy of `nfs3.exe` (angr and capstone):
`xref`, `field car 0x5b0` (every access to a struct's field), `decomp`
with the docs' names applied, and the hand-made name map `curated.py`.
They keep no game data; their caches go to `build/nfsre`. See its
README.md.

## Open questions

- How the computer racers meet each other: the page's cars now have
  the game's forward force and gravity on the hills (`0x4076b0`,
  `0x4a2a0c`), the car-to-car box test and impulse (`0x4310c0`; on the
  game's own records it gives the game's contact point, velocities,
  spins and slide steps, physics.md, The hit's arithmetic) and the
  steered point and slice and the sliding controllers' push as the code
  has them ([frd.md](../formats/nfs/frd.md), Car 3's lane at 61.5 s,
  The hits between cars). The side offset is now measured along the
  road polygon's right, the slice is the track position's and a lane's
  length between slices follows the bend (frd.md, Where the cars part):
  over the first 1.7 s from `t6-log`'s 50 s dump the side offsets are
  within 0.07 m of the game's 16 ms dumps (0.16 before) and the slices
  agree in 96 % of samples (52 %). Car 3 now gets ahead of car 4 by
  slices 90-107, but in the same lane from 62 s and then at 2-5 m,
  where the game's run side by side to slice 91 and car 3, turned by the
  hits, swings out to −11.7. The tick now runs in the game's order (the
  race task's +0x7B8, +0x638 and the car's span across the road +0x21C
  for every car, then every driver, then the models, then the hits) and
  a car's lane edges take that span, not its half width (frd.md, The
  tick's order and the lanes a car spans): to 56 s the decisions agree
  with the game's in 281 of 338 (242), +0x7B8 is within 0.35 m rms over
  the first 3.2 s (0.82). The first differing decisions (51.3-52.7 s)
  now come from the page's model across the road at a slice change on a
  bend, and from 60 s cars 3 and 4 run in other lanes than the game's
  (86 lane changes against the game's 143, 168 samples past 8 m against
  162). The rails controller pushes along the road polygon's right and
  forward, and a car that has reversed more than 8 times, or has been
  off the lanes and slow for 1000 ticks, is put back on the road
  (`0x429100`). A slow hit's reversing (`0x406120`) and the roll and
  pitch a hit gives are ported (frd.md, Reversing after a slow hit). The road
  search's marks of the other side near bends' walls are what the
  code does (frd.md, The search beyond its first slice); a car against
  such a wall isn't pushed the wrong way by it: the wall test steps
  from the car's own polygon and makes its normals from that polygon's
  edges, and a mark of the other side finds road there (read, and driven
  on Rocky Pass, `e5-wall1`). Since `g1` gravity pulls along the body's
  own forward, and the body's tilt is the world's, brought back to the
  road's only past 0.02 (frd.md, Gravity along the body's own forward):
  over `t6-log`'s window the speeds are 3.09 m/s rms from the game's
  (4.41), 1.06 from 50 to 64 s (1.57), and the page's cars no longer run
  side by side into each other (no hits; the game's cars hit 9 times,
  cars 3 and 4 at 64 s, which the page's still don't reproduce). Since
  `h1` +0x7B8 is measured from the body's centre, which the contact keeps
  with the box's lowest corner on the road polygon's plane (from the
  dumped +0x98 the page's measure now matches the game's +0x7B8 to
  0.05 mm rms, 2.7 mm before), and the downforce (`0x4a2260`) and the
  contact's removal of the velocity into the road are ported (frd.md,
  The body's centre and the downforce). Since `i1` the contact's
  velocity step is the code's (frd.md, The contact's frame, recorded a
  step at a time): the velocity goes through the car's 16.16 road rows
  and back, which takes about 0.002 m/s a call off the speed, and
  through rows turned half way to the next polygon's for the ticks the
  box's front is on it; the body turns towards that frame's up. Per
  call the page now agrees with the game's sub-steps to within 0.0003
  m/s for cars 1-3 (0.0024-0.0029 before), the decisions to 56 s in 328
  of 338 (296), the speeds 0.68 m/s rms from 50 to 64 s (1.60). Cars 3
  and 5 still part at 55.7-56.4 s on the 4 m lane edge to a few
  centimetres, so the game's hits (cars 3 and 4 at 64 s) don't come.
  Since `o1` the decisions have been recorded per driver call (every
  routine's votes, the cars ahead and behind, the speed wanted and the
  pick) and fed to the page's driver as the game had them: after one
  fix (the speed table has no 16-byte header, so the page had read its
  speeds and best lines 32 slices late; physics.md, speedsF.bin) the
  page decides as the game does on all 1,584 decisions from 49.9 to
  78.2 s. The sim's own run still parts at 52.3-53.3 s, each time
  because another car's +0x7B8 is a few millimetres to centimetres
  across the 4 m lane edge from the game's (frd.md, The decisions
  recorded per call); speeds are 2.82 m/s rms over the window (3.32).
- How the wall hits' particles look in play (read from the code only;
  their sounds are measured, not listened to), and wall code 3's
  material: the code tells it from code 5 only by its sound (material
  16, not 17), and no name for it is in the exe, so only the tracks'
  look could name it ([col.md](../formats/nfs/col.md)). The second set
  of clips (`Rain`, `Snow`, `Night`, November 2026) shows a car hit's
  sparks and smoke, the wheels' spray in the wet and the snow, and a
  snow-bank hit near the Summit's town, but no wall hit's sparks near
  the camera and no hit that can be placed on a code-3 slice.
- Which renderer the footage's game used. The `Rain` clip shows no fog
  where Hometown's weather sky asks for density 50 and the 3dfx path
  (nfs-host, the Tracks page) draws a heavy haze; the game sends fog
  only to the 3dfx renderers, so the clips are probably from `d3da` or
  a Glide renderer without table fog ([hrz.md](../formats/nfs/hrz.md),
  The weather against footage).
- How a real Pro Logic decoder of the time (with its surround delay
  and 7 kHz low-pass) would place the voices behind; only the matrix's
  sums are measured. The moving objects' sounds and their fades are
  heard in recordings with the engines and music muted, each against a
  twin run without that sound (`e3-ec-fade`, `e3-rr-m2`); with the
  engines and music on, Redrock's sound 8 adds about 1.4 dB over its
  2 s, as much as the mix moves by itself (`e5-rr-m3`; [bnk.md](../formats/nfs/bnk.md),
  The moving objects' sounds). By
  night or in the wet Hometown skips its objects' sounds (`0x41bc20`)
  and Summit plays into an empty voice. The music's rules (place bonus,
  one-racer tables, 8 laps, the rock or tech stream and a chosen tune,
  the zone pairs that count a lap) are checked in runs
  ([mus.md](../formats/nfs/mus.md), [tracks.md](../formats/nfs/tracks.md));
  the backward pair (4, 5) counts on all six tracks that have it. The MMX
  mixer's 8-bit routines run only in `nfs-mmx-check`: no retail sound
  is 8-bit ([bnk.md](../formats/nfs/bnk.md)).
- The car handling in full: the tyre model, steering and gear changes,
  landing, and the computer cars' simpler model. The race types, the
  network kinds, `carp.txt`'s place in memory, the engine and the car
  dynamics task `0x4a2830` are in
  [physics.md](../formats/nfs/physics.md).
- Whether the Classics release differs from the original 1998 retail in
  data or code. Other releases (UK, German, patched) need their own
  hashes. The `Rain`, `Snow`, `Night`, `Walls`, `Sparks`, `HP_Spikes`,
  `HP_RacerList` and `HP_Disabled` clips were recorded from the user's own copy of
  this CD (November 2026: the CD's `nfs3.exe`, `d3da.dll`,
  `voodoo2a.dll`, `voodooa.dll`, `softtria.dll`, `eacsnd.dll` and
  `readme.txt` hash the same as the
  install studied; `nfs3.exe`'s version resource reads file version
  `27, 2.0`, the string the network lobby sends, net.md), in a virtual
  machine through Direct3D (`d3da.dll`, 1998-08-10). The CD's online
  update was never run on that install (the user, November 2026), so
  those clips are this exe. The CD's `Patch/Voodoo3` folder holds a later
  `voodoo2a.dll` (1999-04-21, file version 1999.04.21.1112, 110,592
  bytes against the install's 1998-07-31 93,382) and a new
  `3dSetup/3ddata.dat`, for Voodoo3 cards only; it isn't studied. The
  Hot Pursuit clips found on YouTube (no release named) were taken
  for another release on two counts, the spike icon and the racer
  list. The user's own Hot Pursuit clips (`HP_Spikes`,
  `HP_RacerList`, November 2026; physics.md, "Seen in the user's own
  clips") settle the first: the YouTube spike icon never dims, the
  "dim" being a fault of the measuring script, and no strip is laid
  in those clips, so they show nothing about it. The second stands:
  the user's copy writes the racer list's "over" text, as the code
  and our runs do, and the YouTube clips never show it, though
  nothing in the code that writes or draws it tests an option. (Their
  HUD layout also differs from this install's English `edef.pos`,
  which the user's copy follows, but they use the dial HUD in KPH and
  a small map, both options, so that alone proves nothing.) So those
  clips are from another release or a modified game, not known which;
  nothing else seen in them is put down to that.
