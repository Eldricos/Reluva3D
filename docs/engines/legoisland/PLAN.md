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
4. **A hosted spike** (in progress): `ISLE.EXE` and `LEGO1.DLL` loaded in
   the x86 interpreter, `reluva-tool legoisland run` saying how far they
   get ([systems/running.md](systems/running.md)). The gate: does it
   reach the intro's first frame, and how fast does it run? Both start;
   the program won't go on without DirectSound, which is now stood in
   for ([directsound.md](../../common/x86/directsound.md)). It now makes
   its window, DirectSound, DirectDraw and, through DirectX 5's
   Direct3D, its 3D device, and Retained Mode's scene, and runs its main
   loop with its streaming threads reading the SI files and `WORLD.WDB`,
   showing black frames, and reads `WORLD.WDB`'s models. Next: the rest
   of Retained Mode as the game reaches it, textures first, then meshes
   and drawing.
5. **DirectDraw and DirectSound:** the intro movie and the Information
   Center, its pictures, animations and speech. DirectSound is a new
   stand-in in `src/emulation/x86/directx/`, on the shared mixer.
6. **Direct3D Retained Mode:** a stand-in for the part of it `LEGO1.DLL`
   uses (found by logging its calls), drawn by the existing software
   rasterizer: the island in 3D, driving, building a vehicle.
7. **Input, saves and settings:** DirectInput and the joystick, the save
   folder, and `CONFIG.EXE`'s choices as the engine's settings.
8. **A full playthrough**, checked against recordings of the original;
   then speed (the JIT's hot spots, Retained Mode on the GPU).

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | An engine for this one game, `legoisland` | `LEGO1.DLL` ("LegoOmni Library") isn't known to have run another game; if one is found, the engine moves to the family layout ([DOCUMENTATION_STANDARD.md](../../DOCUMENTATION_STANDARD.md#engines-for-a-family-of-games-and-for-one)) |
| November 2026 | Run the game's own code in the x86 interpreter, with Windows and DirectX stood in for, as LEGO Alpha Team and NFS III are; a gate after the spike (Phase 4) | Nearly all of the game is in `LEGO1.DLL` (1.1 MB of compiled C++). Rewriting it natively from our own reverse engineering would take years, and hosting it plays as the original by construction. The stand-ins it needs (Retained Mode, DirectSound) are shared code, written from Microsoft's published DirectX documentation |
| November 2026 | The community's write-ups of the file formats (the legoisland.org wiki, SIEdit's notes) are read as published references, every fact checked against the game's files; the public decompilation of the game's code isn't read | CONTRIBUTING.md: every line is Reluva's own. Reading another project's reconstruction of the game's code and writing it again would be translating it |
| November 2026 | An import copies the CD's scripts into the game's folder, beside the installer's files | The game then runs with no CD, and its two folders (`diskpath`, `cdpath`) can be the same one; 520 MB is small for a disk today |
| November 2026 | The ImHex patterns for LEGO Island's files (in the `isle-documentation` repository) aren't used, for `WORLD.WDB` or any other file; those files' layouts are worked out from the files themselves, with the legoisland.org wiki's general descriptions | They sit in a fork of the decompilation project, with no license, and don't say where their layouts came from: they may have been read from the decompiled code (see the decision on the decompilation, above) |
| November 2026 | The CD's Microsoft DLLs (`D3DRM.DLL`, `DDRAW.DLL`, `DSOUND.DLL`...), `MFC42.DLL` and `CONFIG.EXE` aren't run | Reluva stands in for DirectX itself; `CONFIG.EXE`, the only user of MFC, is a settings dialog the launcher's settings replace |
