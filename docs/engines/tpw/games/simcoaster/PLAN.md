# SimCoaster: plan

## Goal

The US release played from its own installed files, as the original
plays it: its menus, the three worlds and their missions and challenges,
building and running a park, designing coasters and riding them, with
its sound and speech. Then Theme Park Inc and Theme Park World on the
same code.

## Phases

The program can't be run or read ([executable.md](formats/executable.md)),
so the game is rebuilt from its data, published write-ups and recordings
of the original. Readers and a data viewer first, then the game's
screens, then its rules. Each phase ends with something visible in
Reluva, unit tests on data built in the tests, and a page for each
format or system worked out.

1. **Detection** (done): the US release by its data files; any other as
   "unknown release" ([README.md](README.md#releases)).
2. **Importing from the CD** (done): the CD's InstallShield 6 cabinet,
   read by the shared InstallShield code
   ([installshield.md](../../../../common/installshield.md)), and its
   language's folders, laid out as setup lays them ([cd.md](formats/cd.md)).
3. **The archives and text** (done: [wad.md](formats/wad.md),
   [sam.md](formats/sam.md), [str.md](formats/str.md),
   [bf4.md](formats/bf4.md); the postcard font's packed glyphs wait for
   the postcards, phase 11): the `DWFB` archives (`.wad`), the settings
   files (`.sam`), the string tables (`BFST`, `.str`) and the fonts
   (`F4FB`, `.bf4`); every archive mounted, loose files over them.
   `reluva-tool tpw` commands to list and extract them.
4. **Shared EA formats** (done): RefPack, the pictures and the sound
   streams, first read for Need for Speed III, moved to the shared code
   ([refpack.md](../../../../common/refpack.md),
   [fsh.md](../../../../common/image/fsh.md),
   [ea-streams.md](../../../../common/audio/ea-streams.md)), and taught
   SimCoaster's compressed pictures ([fsh.md](formats/fsh.md)). The logo
   movies' sound (`Data/movies/*.tgq`) reads and decodes with the shared
   stream reader: 3.24 seconds of stereo EA-XA each.
5. **The data viewer** (done): `reluva-viewer`'s pages for the pictures,
   fonts, text, settings, sounds ([sdt.md](formats/sdt.md)), movies
   ([tgq.md](formats/tgq.md)) and files; the opening as a recording shows
   it ([opening.md](systems/opening.md)). The sound banks' `.map` files
   wait for the game's sounds to be played.
6. **Models and the park** (under way): the models (`.MD2`, Bullfrog's
   own: read and drawn in the viewer, [md2.md](formats/md2.md); their
   animations not yet) and their height maps (`.hmp`: read,
   [hmp.md](formats/hmp.md)); the worlds' terrain (the surroundings, the
   park's ground and attribute maps: read and drawn,
   [lnd.md](formats/lnd.md), the ground's painting guessed); objects
   (`scape.omp`) and skies: an empty park drawn in 3D in the viewer.
7. **The ride scripts:** the rides' and shops' behaviour is in compiled
   scripts (`RSSE`, `.rse`, 288 of them): a disassembler command, then an
   interpreter, so rides run in the viewer.
8. **The front end:** the start-up screens (`Data/Init/`), the main menu,
   options, saving and loading, from `ui.wad` and recordings.
9. **The park running:** building, paths, guests, staff, money, research,
   the advisor, missions and challenges (`missions/*.sam`,
   `Challenges.sam`). By far the largest part: from the settings files,
   recordings, and saves (`.TPWS`) taken a step apart.
10. **Coasters:** the designer, riding them, the coaster files (`.cos`)
    and importing them.
11. **Sound and the rest:** music, speech with its lip-sync (`lips.wad`),
    QSound's 3D sound on Reluva's mixer, weather, particles (`.plb`),
    the postcards.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | An engine for the family (`tpw`), with SimCoaster in `games/simcoaster/` | SimCoaster is Theme Park World's engine and formats (its settings files still name it); Theme Park Inc and Theme Park World are to follow on the same code |
| November 2026 | Rebuilt from the data, not run in the x86 emulator | `Game.exe` is under SafeDisc 2.05, its code and data encrypted ([executable.md](formats/executable.md)) |
| November 2026 | No decrypted or "no-CD" executable is used, to run the game or to read its code | Getting round the protection is a legal question for each contributor; the data, the manual, recordings and published write-ups are enough, as for Birth of the Federation and Midtown Madness 2 |
| November 2026 | Detection keys on the data files (`Data/ui.wad` and the American string tables), not on `Game.exe` | A replaced executable shouldn't stop a copy being recognised; the string tables' folder tells the US release from the European ones |
| November 2026 | The OpenTPW project's format notes are read for file layouts only, every field checked against the data | They're public documentation of Theme Park World's formats; no code is copied |
| November 2026 | The online services (`wea*.dll`: chat, mail, uploads, voting) are left out | Their servers have been shut down |
| November 2026 | Importing from the CD takes the game's data, movie and `Game.exe`, not its DLLs, SafeDisc's files or EA's registration program | Reluva doesn't run the game's program, so nothing it loads is needed; `Game.exe` is kept for detection and for anyone who wants to run the original from the folder |
