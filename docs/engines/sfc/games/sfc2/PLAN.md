# Star Trek: Starfleet Command II: plan

## Goal

Star Trek: Starfleet Command Volume II: Empires at War, the English
retail release and its 2.0.3.6 patch, played from the player's own
install: the front end, the tutorials, skirmishes, the single-player
campaigns on the Dynaverse with their missions, the film room, and
multiplayer between copies of Reluva. Internet play through WON's and
GameSpy's servers is gone with them.

### What the install and the CDs hold

Surveyed November 2026 from the owner's copy: an install, both CDs, and
the patch ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#sources)).

| Files | What | Format |
|---|---|---|
| `StarFleet2.exe` (4,149,248 bytes in 1.0) | The game. Visual C++ 6 with its C runtime linked in; no copy protection (only multiplayer asks for the CD key) | PE; imports DirectDraw (`IDirectDraw2` and `4`, `IDirectDrawSurface3` and `4`), Direct3D 6 (`IDirect3D3`, `IDirect3DTexture2`), DirectInput, DirectSound 3D, DirectPlay 4 and its lobby, Winsock, WinMM, Miles (`mss32.dll`, 25 `AIL_*` functions) and WON's library |
| `SFC2.exe`, `SFC2Setup.exe` | A launcher, and the resolution and driver setup (MFC) | PE |
| `Assets/Scripts/*.scr` (49 in 1.0) | The missions: Win32 DLLs exporting `ScriptEntry` and `MissionSpecEntry`, loaded by the game | PE |
| `Assets/Scripts/Campaigns/*.mct` | A campaign: its three maps, its missions | INI-like text |
| `MetaAssets/*.mvm`, `MetaMap.war` | The Dynaverse: hexes, regions, terrain | INI-like text with a binary block |
| `*.gf`, `MetaAssets/ServerProfiles/` | Settings: economy, time, score, the campaign server's | INI-like text |
| `Assets/specs/shiplist.txt`, `ftrlist.txt`, `bpvdata.ini` | Every ship and fighter: hull, BPV, weapons, systems | Tab-separated text |
| `Assets/MODELS/<ship>/*.mod` (102) and `.bmp` (890) | Ship, base and asteroid models, with `_brk` broken ones, and their textures | Chunked binary (`MDLS`, `STRS`...) |
| `Assets/Sprites/sprites.q3` (78 MB) | The interface's pictures | Binary archive (`MDM!`) |
| `Assets/aafonts/*.bft` (50) | Bitmap fonts, by race and size | Binary |
| `Assets/Sound/*.zip`, `sounds.txt`, `SFCMusic.txt` | Effects, voices, music, and their tables | Zip; tab-separated text |
| `Assets/Strings/*.txt` | The game's text | Text |
| `Assets/mouse/*.cur` | Each race's pointer | Windows cursors |
| `SFC2Intro.exe` (69 MB) | The opening movie: a self-playing Bink file (revision `i`) | PE with a Bink file inside |
| `MetaServer/ServerPlatform.exe` | A dedicated campaign server's launcher | PE |
| `API/` | Taldren's mission-script kit | Not used ([Decisions](#decisions)) |
| `SFB/*.pdf` | Star Fleet Battles' Cadet rules | PDF |
| CDs: `data1.hdr`, `data1.cab`, `data2.cab` | The install, in InstallShield cabinets | Version 5 (0x01005201); Reluva reads version 7's |
| `sfc2patch20022036.exe` | Patch 2.0.3.6: a self-extracting Microsoft cabinet holding an InstallShield 5 setup, which installs an RTPatch file (`PATCHSFC2-2002-2036.RTP`) and Pocket Soft's `Patch.exe` to apply it | It replaces `StarFleet2.exe`, almost every mission, 25 models and most settings files, and adds missions |

## Phases

Each phase ends with something that runs and is committed, and its facts
on their pages in `formats/` and `systems/`.

1. **The engine's start** (done): the template, with the docs laid out
   for a family of games.
2. **Detection** (next): `StarFleet2.exe`, `sprites.q3` and
   `shiplist.txt` hashed with `reluva --hash`, for 1.0 and for 2.0.3.6;
   `kVariants` with both, and other releases found by their files as
   "(unknown release)".
3. **Readers and a data viewer:** `sprites.q3`, `.bft`, `.mod` with its
   textures, `shiplist.txt` and `ftrlist.txt`, the `.gf`, `.mct` and `.mvm`
   text, the sound zips with `sounds.txt`, the Bink intro (shared
   `src/video/bink.cpp`, which reads revision `i`). Each format worked out
   from the files themselves and checked on every retail file. Needed
   whichever way the game runs.
4. **Hosting spike (a gate):** `StarFleet2.exe` loaded in the x86
   emulator (`src/emulation/x86/`) as `nfs` runs NFS III: how far it gets
   towards its first frame, what it calls that nothing stands in for, how
   fast it runs.
5. **Shared stand-ins it needs**, in `src/emulation/x86/`, each on its
   page in `docs/common/x86/`:
   - Direct3D 6's interfaces (`IDirect3D3`, `IDirect3DDevice3`,
     `IDirect3DTexture2`, `IDirectDraw4`, `IDirectDrawSurface4`) on the
     Direct3D 7 objects already there;
   - Miles' 25 `AIL_*` sample functions, on the shared mixer;
   - DirectSound 3D buffers, if Miles doesn't take all the sound.
6. **The game hosted:** the front end, a skirmish, the tutorials, then a
   campaign and its missions (the game loads their DLLs itself), saves,
   the film room. Drawing on the GPU through `gfx3d` once the software
   rasterizer is too slow.
7. **Multiplayer:** DirectPlay 4 over the shared networking, between
   copies of Reluva.
8. **Importing:** InstallShield 5's headers in
   `src/common/installshield.cpp`, and the `MetaEngine`'s `planImport`,
   from the CDs. Applying the 2.0.3.6 patch as it's imported, by running
   its `Patchw32.dll` in the emulator, if RTPatch's format can't be read.
9. **Native, part by part:** the game's own code replaced where that
   pays (drawing, sound, the interface, the simulation), each part hooked
   at the functions that do it, as `nfs` draws THRASH's triangles and mixes
   its sound natively ([Decisions](#decisions)).
10. **The game-files test** and a full playthrough recorded in
    [TESTING.md](TESTING.md).

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | The engine is `sfc`, for a family of games; this game is `sfc2` | Starfleet Command, its Volume II and Orion Pirates share Taldren's engine and files; the standard asks for `games/<id>/` from the start for a family |
| November 2026 | The game hosted first (its own `StarFleet2.exe` in the x86 emulator), with native readers for the data viewer; confirmed at the spike's gate | The combat simulation, the AI and the Dynaverse are most of the game and what players remember. Its missions are compiled C++ DLLs built against the executable's own classes: a native engine would have to rebuild that interface exactly before any mission ran, while hosted the game runs them itself. The executable has no copy protection, and the stand-ins it needs are close to those `nfs` and `netimmerse` use |
| November 2026 | Hosted can become native later, a part at a time | The readers and viewer are native from the start; then a hooked function at a time, as `nfs` replaced THRASH and its mixers, until the executable runs only what's left. The missions are the last part: native, they'd need the interface between the game and its DLLs, worked out from the executable and the DLLs themselves; until then they run in the emulator |
| November 2026 | Taldren's mission-script kit (the install's and the second CD's `API/`) isn't used | The owner's choice. It's the developer's own code, under no licence that would let it into Reluva, and the hosted game doesn't need it. The survey listed its files and read its readme's installation steps; no header, source or library of it was opened ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#sources)) |
| November 2026 | Patch 2.0.3.6 is supported as a release of its own | It's the last release, and the one players run. It was made by the KhoroMag patch team in 2002; it's run as the game, and its executable is studied as the retail one is |
| November 2026 | Star Fleet Battles' rules (the Cadet PDFs in the install, the published rulebooks) are a source for how combat works | Published by the board game's publisher; the game is built on its rules |
| November 2026 | The community's write-ups on the files are read for layouts only, each field checked against the files | As `botf` reads its community's editor: public documentation, no code copied |
