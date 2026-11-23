# Need for Speed II SE: plan

## Goal

Need for Speed II: Special Edition (Electronic Arts, 1997), game id
`nfs2se`, as the second game of the `nfs` engine, beside NFS III. Its own
code goes in a folder of the engine's, `nfs2se/`, in
`reluva::nfs::nfs2se`; what it shares with NFS III stays in
`src/engines/nfs/`.

"Done" is the retail game played as it was: its front end, every race
type on its eight tracks, its music and movies from the CD, single
player and over a network. It runs in both of its renderers: the
software one (`nfs2sen.exe`) and the 3dfx one (`nfs2sea.exe`, Glide 2).
It plays from the US/English CD (English and Spanish set-up) and an
install made from it. `reluva-viewer` gets a data viewer of its files.

What the retail files hold, and how far NFS III's readers already get
with them, is in [research/first-survey.md](research/first-survey.md).

### Why a game of the `nfs` engine, not a new engine

The `nfs` engine was made for this family (its README; NFS III's
[PLAN.md](../nfs3/PLAN.md), Why one engine for a family), and the
survey bears it out:

- **The same toolchain.** Both executables were built with Watcom
  C/C++ (sections `AUTO`/`DGROUP`, linker 2.18), as `nfs3.exe` was, and
  they import nearly the same Win32 calls (DirectDraw, DirectInput,
  WINMM's timer, WSOCK32, TAPI, the serial port).
- **The same sound interface.** iSND, in an older `eacsnd.dll`
  (6 exports, Watcom register calls) over DirectSound.
- **Most of the same file formats.** Reading the whole CD and install
  with NFS III's readers (`reluva-tool nfs survey`): all 6 VIV
  archives, all 284 sound banks (838 sounds), all 19 music maps and all
  14 fonts read; 363 of 595 picture banks; `COLL` version 11 is the
  same magic and version.

What's new is NFS II's own: `TRAC` tracks (`.trk`), `.geo` car models,
its sky files, the `mTCD` movies, and the front end's data.

### Two ways to play it

As with NFS III: **hosted** (the retail executable in Reluva's x86
emulator) or **native** (everything rewritten from the data).
**Recommendation: hosted**, with the data layer and viewer first:

- NFS III's hosting already stands in for nearly every call NFS II SE
  makes: Windows, DirectDraw, DirectInput, DirectSound, WINMM, winsock
  and serial (`src/engines/nfs/host/`, `src/emulation/x86/`).
- NFS II SE has no THRASH layer: `nfs2sen.exe` draws in its own code
  into DirectDraw surfaces, and `nfs2sea.exe` calls 3dfx's `glide2x.dll`
  itself (`LoadLibraryA`, 33 Glide 2 functions), which Reluva's Glide
  stand-in already serves ([glide.md](../../../../common/graphics/glide.md)).
  So there's no renderer interface to write.
- Native would mean matching NFS II's handling, AI and front end by
  watching alone, the bulk of the game. It stays the fallback, and the
  data layer serves either way.

The risk is speed: the software build draws every pixel inside the
emulator. Phase 4 measures it before anything depends on it.

## Phases

Each phase ends with something that runs, is tested, and is committed,
with what it found written on the game's pages in `formats/` and
`systems/`, each fact with its source and confidence
(DOCUMENTATION_STANDARD.md).

0. **Each game's own folder** (done): NFS III's own code moved to
   `src/engines/nfs/nfs3/`, in `reluva::nfs::nfs3`: its formats NFS II
   SE doesn't have (`.fce`, `.frd`, its `.hrz`, `.mad`), its tracks'
   particles, and the viewer's Tracks and Cars pages, each now with its
   own header. What both games read stays shared. The moves are listed
   in `tools/relocations/2026-11-nfs-games.txt`. This folder, with its
   five pages.
1. **Skeleton and detection** (done):
   - `nfs2se` in `games.cpp`'s game list, its detection entries from the
     table in [first-survey.md](research/first-survey.md) (the
     software and 3dfx executables, the install and the CD), and a
     fallback by file names;
   - `folders.cpp` finding the CD's files (`install.win`'s `D:` folders:
     the tracks, music, movies and speech);
   - the engine's Play page saying what it found for an `nfs2se` target;
   - unit tests of detection and folder finding on folders the tests
     build.

   Built: the game in `games.cpp` (`Game::kNfs2Se`), recognised by
   `nfs2sen.exe` and `cardata.viv`, which the install and the CD both
   have, as the tracks are on the CD alone; `checkFolders`, which wants
   NFS III's track folders or NFS II SE's `.trk` files (on the CD); in
   `src/engines/nfs/nfs2se/`, its tracks, car models and programs found
   (`files.cpp`) and the Status page the game shows
   (`status_page.cpp`), also the viewer's first page. Checked on the
   retail install and CD ([TESTING.md](TESTING.md#last-checked)).
2. **The shared formats, with NFS II's differences** (done):
   - pictures: SHPI types `0x2A` (a palette entry shared by the bank's
     pictures), `0x24`, `0x7A`, `0x7D` and `0x7B` drawn with the bank's
     palette; the pause screens' pictures with no palette of their own
     (5,266 pictures failed only for this);
   - the `.qda` files (`30 FB`, which is EA's Huffman scheme, already
     read: [refpack.md](../../formats/refpack.md));
   - the text tables (`text.*`, `textsim.eng`, `gf8text.eng`), whose
     first offset NFS III's reader rejects;
   - `.mus` files holding several streams with padding between them;
   - the pages these shared formats are on move from NFS III's folder
     to the engine's `formats/` (viv, refpack, fsh, ffn, bnk, mus), as
     DOCUMENTATION_STANDARD.md asks once a second game shares a format,
     each noting how the two games differ;
   - the viewer's Textures, Strings, Fonts, Sounds and Music pages
     working for an `nfs2se` target;
   - `reluva-tool nfs survey` reading every file with no failures.

   Built: the pages moved; the picture reader reads a bank's palette
   entry and gives it to the pictures without one, 4-bit pictures and
   `0x29` palettes; the Textures page shows the pause screens, which have
   no palette, in the race's (`hud.fsh`'s), as their 3dfx copies show the
   game does; the text reader reads NFS II SE's front end's records with
   each string's place, and the race messages (`textsim.*`); the music
   reader skips the zero padding between streams. `reluva-tool nfs
   survey` reads every file of these kinds on the CD and in the install
   with no failures, counting apart the pause pictures, three empty banks
   in `huddata.viv` and the installer's placeholder `temp.mus`. What's
   left failing is NFS II's own `.col` and `.hrz`, for the next phase.
3. **Tracks and cars in 3D** (done):
   - `TRAC` tracks (`gamedata/tracks/se/trNN.trk`) and their texture
     banks, NFS II's `.col`, sky (`.hrz`, 26-27 numbers to NFS III's
     more), lights (`.lgt`, commented text) and camera paths (`.can`);
   - `.geo` car models (in `cardata.viv`) with their `.qfs` skins;
   - the Tracks and Cars viewer pages, and `reluva-tool nfs` commands
     to check every track and model (as `frd-check`, `fce-info`).

   The readers are done: [trk.md](formats/trk.md), [col.md](formats/col.md),
   [hrz.md](formats/hrz.md), [geo.md](formats/geo.md), every file of
   theirs read (`reluva-tool nfs survey`), and `reluva-tool nfs trk-obj`
   and `geo-obj` to look at tracks and models elsewhere. The camera paths
   (`.can`) aren't read yet.

   The Tracks and Cars pages draw them, checked under Xvfb on Mesa's
   software Vulkan ([TESTING.md](TESTING.md#last-checked)).
4. **Movies** (done): the `mTCD` video in the `SCHl` streams (`.dct`, EA's
   intra-frame DCT codec), likely shared code in `src/video/` with a
   page in `docs/common/video/`, as other EA titles of the time used it;
   the Movies page; the attract-mode demos (`demo.0`-`demo.2`).

   Built: the frames are the PlayStation's MDEC frames, decoded by
   shared code (`src/video/mdec.cpp`, [mdec.md](../../../../common/video/mdec.md)),
   with MPEG-1's tables moved out of NFS III's MAD decoder to
   `src/video/mpeg1.h` for both; the movies read by `DctMovie`
   ([dct.md](formats/dct.md)) and played with their sound on the Movies
   page. All 15 movies' 9,340 frames decode, matching FFmpeg's pictures
   to within rounding. `demo.0`-`demo.2` turned out not to be movies:
   three copies of one file, left for Phase 6, when the game runs.
5. **The hosted spike, and its gate** (done):
   - make `GameHost` game-neutral: what's NFS III's own (its patched
     addresses, THRASH, its native routines: `host/thrash.cpp`,
     `host/thrash_native.cpp`, `render/thrash_gpu.cpp`) moves to
     `src/engines/nfs/nfs3/`, so the Windows, DirectX, sound and network
     stand-ins serve both;
   - run `nfs2sen.exe` to its first front-end frame, logging its calls
     (`reluva-tool nfs host`), and stand in for what it lacks (today it
     stops in the generic `x86-run` at `VirtualQuery`);
   - measure the speed of a race in the software renderer;
   - gate: hosted if it plays at full speed, else what it would take.

   Built: `GameHost` runs a program it's given (`Options::program`;
   `reluva-tool nfs host` picks `nfs2sen.exe` in an install without
   `nfs3.exe`). NFS III's own parts already stood aside for another
   program: each patch checks the code it patches first, and THRASH is
   only a DLL exporting `THRASH_` functions. What NFS II SE needed was
   Windows' own behaviour: the keyboard hook called with each key,
   drawing into a flipping pair's front surface shown, and
   `joyGetNumDevs`. NFS III's THRASH classes moved to `nfs3/host/` and
   `nfs3/render/`; `GameHost`'s own THRASH and mixer methods stay in
   `host/` until NFS II SE has parts of its own there.

   `nfs2sen.exe` plays its EA logo and title movie, shows the front end,
   and starts a race at Proving Grounds, its car driven with the keys
   ([research/hosted-spike.md](research/hosted-spike.md)). The race
   draws about 39 frames a second in the software renderer, 7.7 million
   instructions a frame, the emulator running faster than real time.

   **Gate: passed.** The game is hosted, as NFS III is.
6. **Hosted play** (done, but for what's left open below): the front end, every race type, the iSND sound
   through its own `eacsnd.dll`, the CD music and movies, the attract
   mode (what `demo.0`-`demo.2` are), input and
   force feedback, saves in Reluva's save folder; the game-files test
   (its steps in `tests/engines/nfs/gamedata.cmake`, as `nfs.nfs2se`).

   Built: the launcher's Play page runs `nfs2sen.exe` when the install
   has it, the viewer's too (`PlayPage::setProgram`); an install with
   only the 3dfx program still shows the Status page. The menus take
   every key: the host delivers input by the clock, its window thread
   running at once, as Windows does (the game reads its key buffer twice,
   `0x405900`, and lost the keys that came between). The game-files test
   is `tests/engines/nfs/gamedata-nfs2se.cmake`, its own script, its host
   step from the title movie to a race; on the instruction clock the
   date is fixed, so the run is the same each time.

   Checked: the movies, front end and attract mode (the `demoav*.dct`
   movies with the menu music); single race, tournament and knockout
   started; the sound recorded through the movie, menus and a race, none
   of it played stale; the game's writes (`config.dat`, `TmpTrk/`) in
   the save folder, the install untouched; a joystick offered taken,
   read, and given three force-feedback effects
   ([TESTING.md](TESTING.md#last-checked)).

   Settled afterwards:
   - **Presenting:** a race's picture is shown once a frame, when the
     game stops writing its strips into the front surface (4 ms of its
     clock), not at each of its 9 Unlocks.
   - **Force feedback** plays once the wheel is the game's controller:
     *options*, *controls*, *setup*, each control re-mapped by moving
     the wheel (steering took axis 0, the accelerator axis 1). In a race
     the game then keeps a spring on the wheel whose centre follows the
     road (214 changes in a minute), and a full constant push at two
     impacts; no damper or vibration. Left on the keyboard, the game
     downloads its three effects but never starts them.
   - **`demo.0`-`demo.2`:** neither program names them, nor builds a
     name like theirs, nor does the set-up script; the attract mode
     plays the `demoav*.dct` movies. Unused by the retail game, as far
     as can be seen *(inferred)*.

   Left open: a whole tournament or knockout run to its end.
7. **3dfx** (done): `nfs2sea.exe` on the Glide stand-in, in software and on the
   GPU, as a `renderer` setting.

   Built: `renderer=glide` plays the 3dfx program, the install's or else
   the CD's (`nfs2se::playProgram`; the host runs a program from outside
   the install, `Options::programFile`), always on Glide, as a Voodoo
   Graphics unless `glideboard` names another. That board matters: the
   program passes its vertices snapped and never unsnapped (x + 786,432),
   which Reluva's Glide reads as the first boards did; on the Voodoo2,
   the stand-in's default, nothing showed. Its movies, front end and
   races draw; it needed nothing else of the host. The game-files test
   has a `host_glide` step: the same run as `host`, with this program, on
   the software rasterizer.

   Measured: by the game's clock the race draws about 64 frames a
   second. Drawing in software is what costs: the run took 367 s for 80 s
   of the game, and in the app, with Glide's GPU backend on Mesa's
   software Vulkan (no GPU here), it ran about 20 frames a second. On a
   real GPU it's to be measured.
8. **Network play** (done): UDP (winsock), modem (TAPI) and serial, on
   NFS III's stand-ins.

   Built: nothing new was needed of the host. Two copies of
   `nfs2sen.exe` on one machine raced Proving Grounds against each other
   over each of the game's three links: the network (IPX, carried over
   UDP), a serial link (one TCP link) and a modem call (TAPI over TCP),
   each from its connect screen through the lobby, ready lights and
   *synchronizing*, the two race clocks within a few hundredths of a
   second ([research/network-play.md](research/network-play.md)). The
   Play page gives the game these links as NFS III's (the `network`,
   `serial` and `modem` settings). The host tool's `-keysfile` drove the
   copies by what their frames showed: on a clock of instructions their
   clocks drift apart and the game's own timeouts fire.

   Left open: races and tournaments run to their end over a link, lost
   packets and a player leaving, copies on two machines, the 3dfx
   program on a link.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | NFS II SE as a game of the `nfs` engine (`nfs2se`), not a new engine | The same toolchain, Win32 calls, sound interface and most file formats as NFS III ([Why a game of the `nfs` engine](#why-a-game-of-the-nfs-engine-not-a-new-engine)) |
| November 2026 | Each game's own code in a folder of its own (`nfs3/`, `nfs2se/`) and namespace (`reluva::nfs::nfs3`...), the shared code at the engine's top, before NFS II SE's work begins | docs/ADDING_AN_ENGINE.md (Code layout) and the coding guide's namespaces, as `awe` and `netimmerse` do; the host waits for Phase 5, as `GameHost`'s NFS III parts are in its own methods, not files apart |
| November 2026 | The data layer and viewer first, then a hosted spike with a gate | As NFS III; hosting keeps the game's own behaviour, and most of it already exists ([Two ways to play it](#two-ways-to-play-it)) |
| November 2026 | Open-source readers (OpenNFS, FFmpeg's EA demuxer and decoders) and community write-ups of NFS II's formats read as references only, no code taken | CONTRIBUTING.md, as for NFS III |
| November 2026 | NFS II SE's network play on NFS III's stand-ins, as they are | All three of its links raced with nothing changed ([research/network-play.md](research/network-play.md)) |
| November 2026 | NFS II SE hosted, through the plan's gate | Its software program races at about 39 frames a second on the host with nothing of its own natively ([research/hosted-spike.md](research/hosted-spike.md)) |
| November 2026 | Denis Auroux's NFS II track notes not used; the `.trk` and `.col` pages rest on the files alone | They forbid using their information commercially, which doesn't sit with the GPL; every fact they gave was worked out again from the files, and the meanings only they gave dropped |
