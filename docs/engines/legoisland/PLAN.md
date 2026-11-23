# LEGO Island: plan

## Goal

The English retail release played from its own files, as the original
plays: the intro, the Information Center, the island in every vehicle,
building the four vehicles, the races, the Brickster's escape and chase
(Acts 2 and 3), saving and loading, the jukebox and the credits.

## Phases

Each phase ends with something that runs, is tested and is committed.

1. **The engine's start** (done): the template, these pages, the plan.
2. **Detection and import** (done): `kVariants` for the English 1.0
   release, keyed on `LEGO1.DLL` and `WORLD.WDB`, and *Import Game...*
   from the CD or an install into one folder
   ([systems/game-files.md](systems/game-files.md)).
3. **Readers and the data viewer** (done): the `.SI`
   archives ([formats/si.md](formats/si.md)), their FLC animations (a
   shared FLIC decoder, [flic.md](../../common/video/flic.md)), Smacker
   movies, WAV sounds and BMP pictures, with a viewer page playing them;
   `WORLD.WDB`'s worlds, models, parts and textures
   ([formats/wdb.md](formats/wdb.md)), drawn in 3D by the viewer; the
   worlds' `*INF.DTA` animation lists ([formats/dta.md](formats/dta.md));
   the SI files' 3D objects: animations, models, textures and paths
   ([formats/obj.md](formats/obj.md)), each worked out from the files
   ([Decisions](#decisions)); the SI files' events
   ([formats/si.md](formats/si.md#evt-an-event)); the viewer drawing the
   SI files' models and playing their animations; and a page for the
   `*INF.DTA` lists. Every kind of file in the release's `lego` folder
   (`data\*.DTA`, `WORLD.WDB`, `Scripts\*.SI`) is read.
4. **A hosted spike** (done): `ISLE.EXE` and `LEGO1.DLL` run in the x86
   interpreter, `reluva-tool legoisland run` saying how far they get
   ([systems/running.md](systems/running.md)). The gate, the intro's first
   frame at a usable speed, is passed: the game loads its world and plays
   its intro movies about as fast as they should go, with DirectSound
   ([directsound.md](../../common/x86/directsound.md)), DirectX 5's
   Direct3D and Retained Mode ([d3drm.md](../../common/x86/d3drm.md))
   stood in for as far as it needs them so far.
5. **DirectDraw and DirectSound** (done): the engine plays the game, its
   program on a thread of its own, its frames shown, input passed on,
   its sound on the mixer through DirectSound: the intro (a click skips
   it) and the Information Center
   ([systems/running.md](systems/running.md#in-reluva)).
6. **Direct3D Retained Mode** (done): a stand-in for the part of it
   `LEGO1.DLL` uses (found by logging its calls), drawn by the existing
   software rasterizer ([d3drm.md](../../common/x86/d3drm.md)): its
   scenes drawn and picked, the island in 3D as a screenshot of the
   original shows it, walked, the skateboard ridden, and the jet-ski
   built and ridden ([systems/running.md](systems/running.md#how-far-it-gets)).
7. **Input, saves and settings** (done, to be tried in the engine):
   DirectInput's keys and WINMM's joystick, the first one connected; the
   save folder, players registered and their games kept from one session
   to the next; what the installer and the setup (`CONFIG.EXE`) write to
   the registry, its choices as the engine's settings (the 3D device
   among them); and the player turning by time, at the speed it was
   made for, where the original turns by the frame, too fast on today's
   machines ([KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)).
8. **A full playthrough** (in progress), checked against recordings of
   the original, by the checklist in [TESTING.md](TESTING.md#a-playthrough-by-hand);
   then speed (the JIT's hot spots, Retained Mode on the GPU). So far:
   every place on the map reached in Act 1, and its routes run by the
   game-files test; Acts 2 and 3 not yet played. Speed: Render skips
   what's out of view, and the draw threads are woken only once enough
   is waiting, and a frame is shown once the next is given, its drawing
   done meanwhile; on four processors the island runs at the game's own
   most, 90 frames a second, above the engine's limit of 60; a
   triangle's set-up is done on the draw threads too. Next, for slower
   machines: Retained Mode on the GPU, begun: a compute kernel drawing by
   the software rasterizer's rules, checked against it
   ([directx.md](../../common/x86/directx.md#drawing-on-the-gpu)), the
   surfaces it draws into kept there between batches, clears, fills and
   copies drawn there too; still to come, the engine using it (a setting,
   software where there's no GPU), and its speed measured on a GPU.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | An engine for this one game, `legoisland` | `LEGO1.DLL` ("LegoOmni Library") isn't known to have run another game; if one is found, the engine moves to the family layout ([DOCUMENTATION_STANDARD.md](../../DOCUMENTATION_STANDARD.md#engines-for-a-family-of-games-and-for-one)) |
| November 2026 | Run the game's own code in the x86 interpreter, with Windows and DirectX stood in for, as LEGO Alpha Team and NFS III are; a gate after the spike (Phase 4) | Nearly all of the game is in `LEGO1.DLL` (1.1 MB of compiled C++). Rewriting it natively from our own reverse engineering would take years, and hosting it plays as the original by construction. The stand-ins it needs (Retained Mode, DirectSound) are shared code, written from Microsoft's published DirectX documentation |
| November 2026 | The community's write-ups of the file formats (the legoisland.org wiki, SIEdit's notes) are read as published references, every fact checked against the game's files; the public decompilation of the game's code isn't read | CONTRIBUTING.md: every line is Reluva's own. Reading another project's reconstruction of the game's code and writing it again would be translating it |
| November 2026 | An import copies the CD's scripts into the game's folder, beside the installer's files | The game then runs with no CD, and its two folders (`diskpath`, `cdpath`) can be the same one; 520 MB is small for a disk today |
| November 2026 | The ImHex patterns for LEGO Island's files (in the `isle-documentation` repository) aren't used, for `WORLD.WDB` or any other file; those files' layouts are worked out from the files themselves, with the legoisland.org wiki's general descriptions | They sit in a fork of the decompilation project, with no license, and don't say where their layouts came from: they may have been read from the decompiled code (see the decision on the decompilation, above) |
| November 2026 | Hosted, after the spike's gate passed | The retail game, run in the interpreter, loads its world and plays its intro at about its own speed (phase 4) |
| November 2026 | The CD's Microsoft DLLs (`D3DRM.DLL`, `DDRAW.DLL`, `DSOUND.DLL`...), `MFC42.DLL` and `CONFIG.EXE` aren't run | Reluva stands in for DirectX itself; `CONFIG.EXE`, the only user of MFC, is a settings dialog the launcher's settings replace |
