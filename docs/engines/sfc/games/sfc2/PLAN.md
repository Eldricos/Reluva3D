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
| `StarFleet2.exe` (4,149,248 bytes in 1.0) | The game. Visual C++ 6 with its C runtime linked in; no CD check, but it asks for its CD key at its title screen | PE; imports DirectDraw (`IDirectDraw2` and `4`, `IDirectDrawSurface3` and `4`), Direct3D 6 (`IDirect3D3`, `IDirect3DTexture2`), DirectInput, DirectSound 3D, DirectPlay 4 and its lobby, Winsock, WinMM, Miles (`mss32.dll`, 25 `AIL_*` functions) and WON's library |
| `SFC2.exe`, `SFC2Setup.exe` | A launcher, and the resolution and driver setup (MFC) | PE |
| `Assets/Scripts/*.scr` (104 in 1.0) | The missions: Win32 DLLs exporting `ScriptEntry` and `MissionSpecEntry`, loaded by the game | PE |
| `Assets/Scripts/Campaigns/*.mct` | A campaign: its three maps, its missions | INI-like text |
| `MetaAssets/*.mvm`, `MetaMap.war` | The Dynaverse: hexes, regions, terrain | INI-like text with a binary block |
| `*.gf`, `MetaAssets/ServerProfiles/` | Settings: economy, time, score, the campaign server's | INI-like text |
| `Assets/specs/shiplist.txt`, `ftrlist.txt`, `bpvdata.ini` | Every ship and fighter: hull, BPV, weapons, systems | Tab-separated text |
| `Assets/MODELS/<ship>/*.mod` (317) and `.bmp` (2,571) | Ship, base and asteroid models, with `_brk` broken ones, and their textures | Chunked binary (`MDLS`, `STRS`...) |
| `Assets/Sprites/sprites.q3` (78 MB) | The interface's pictures | Binary archive (`MDM!`) |
| `Assets/aafonts/*.bft` (126) | Bitmap fonts, by race and size | Binary |
| `Assets/Sound/*.zip`, `sounds.txt`, `SFCMusic.txt` | Effects, voices, music, and their tables | Zip; tab-separated text |
| `Assets/Strings/*.txt` | The game's text | Text |
| `Assets/mouse/*.cur` | Each race's pointer | Windows cursors |
| `SFC2Intro.exe` (69 MB) | The opening movie: a self-playing Bink file (revision `i`) | PE with a Bink file inside |
| `MetaServer/ServerPlatform.exe` | A dedicated campaign server's launcher | PE |
| `API/` | Taldren's mission-script kit | Not used ([Decisions](#decisions)) |
| `SFB/*.pdf` | Star Fleet Battles' Cadet rules | PDF |
| CDs: `data1.hdr`, `data1.cab`, `data2.cab` | The install, in InstallShield cabinets | Version 5 (0x01005201), read by Reluva ([InstallShield cabinets](../../../../common/installshield.md)) |
| `sfc2patch20022036.exe` | Patch 2.0.3.6: a self-extracting Microsoft cabinet holding an InstallShield 5 setup, which installs an RTPatch file (`PATCHSFC2-2002-2036.RTP`) and Pocket Soft's `Patch.exe` to apply it | It replaces `StarFleet2.exe`, almost every mission, 25 models and most settings files, and adds missions |

## Phases

Each phase ends with something that runs and is committed, and its facts
on their pages in `formats/` and `systems/`.

1. **The engine's start** (done): the template, with the docs laid out
   for a family of games.
2. **Detection** (done for 1.0): `StarFleet2.exe` and `sprites.q3`
   hashed; other releases found by their names as "(unknown release)".
   2.0.3.6's hashed from the import, patched as it's imported.
3. **Readers and a data viewer** (done but for some files): `sprites.q3`'s
   images ([formats/sprites-q3.md](formats/sprites-q3.md)), the `.bft`
   fonts ([formats/bft.md](formats/bft.md)), the `.mod` models with their
   textures ([formats/mod.md](formats/mod.md)), the tab and string
   tables ([formats/text-tables.md](formats/text-tables.md)) and the
   sound zips ([formats/sound-zips.md](formats/sound-zips.md)), each
   worked out from the files themselves and checked on every retail
   file, and a page of the data viewer for each. Left for later, when the
   game needs them: the `.gf`, `.mct` and `.mvm` files, and the objects in
   `sprites.q3`. The opening movie, inside `SFC2Intro.exe`, came later: it
   plays before the game ([formats/intro.md](formats/intro.md)).
4. **Hosting spike (a gate)** (done): `StarFleet2.exe` loaded in the x86
   emulator: it runs its start-up, reads its first files and settings,
   makes its window and DirectDraw, and stops in its 3D test asking for
   Direct3D 3, after 44 million instructions at about 61 million a
   second ([systems/hosting.md](systems/hosting.md)). Hosting goes on
   ([Decisions](#decisions)).
5. **Shared stand-ins it needs** (in progress), in `src/emulation/x86/`,
   each on its page in `docs/common/x86/`, in the order the game asks
   for them:
   - Direct3D 6 (done): `IDirect3D3`, its device, viewports, materials,
     lights and textures on the Direct3D 7 device; with it the game
     passes its 3D test and shows its first frame
     ([systems/hosting.md](systems/hosting.md));
   - C++ exceptions dispatched (done), with what loading asked for next:
     KERNEL32's UTF-16 file functions, exact-name searches, `.` in full
     paths, volumes by their roots; the game loads to its title screen
     and its CD key dialog;
   - input reaching the game (done): the game in the launcher's window,
     the mouse and the keys passed to it; with them, `TranslateMessage`
     typing as a US keyboard does, and a keyed blit failing from a
     source with no colour key, so the game draws its pointer itself.
     Its CD key dialog takes typed keys and checks them
     ([systems/hosting.md](systems/hosting.md#its-mouse-and-keys)); the
     owner's key is taken, and kept, with the program's other registry
     writes, in the save folder;
   - the Windows functions the spike stood in for in the engine
     (`src/engines/sfc/host/system.cpp`), moved to the shared layer as
     they're settled;
   - DirectPlay's lobby (done): told no lobby started it, the game shows
     its main menu ([dplay.md](../../../../common/x86/dplay.md));
   - DirectPlay 4 (done), its sessions over TCP/IP between copies of
     Reluva ([dplay.md](../../../../common/x86/dplay.md)); Winsock only
     for `gethostname` and `gethostbyname`, as a game over TCP/IP calls
     nothing else of it (the rest perhaps for the online services, which
     have closed);
   - Miles' 25 `AIL_*` sample functions, on the shared mixer (done): the
     menu music plays, byte for byte the music's WAVs
     ([miles.md](../../../../common/x86/miles.md));
   - DirectSound 3D buffers, if Miles doesn't take all the sound.
6. **The game hosted** (started): the front end (done), a skirmish
   (it runs and is played: steering, targeting and firing with the
   game's keys and its panels; won against a frigate), the tutorials (each runs, its orders
   following what's done, to its debriefing), a campaign (started: its
   introduction, the Dynaverse map, moving from hex to hex, a mission
   offered, accepted and played into, forfeited and back to the map,
   its auto-save loaded, saves made by name and deleted, Convoy Escort
   won),
   then a
   campaign and its missions (the game loads their DLLs itself), saves,
   the film room. Drawing on the GPU through `gfx3d` once the software
   rasterizer is too slow.
7. **Multiplayer** (started): DirectPlay 4 over the shared networking,
   between copies of Reluva. Started: its connections, and a game hosted
   alone with an AI player, started from Multiplayer Startup into its
   mission ([systems/hosting.md](systems/hosting.md#its-multiplayer)).
   Joined from another copy over TCP/IP
   ([dplay.md](../../../../common/x86/dplay.md#the-network)) and started
   in both, the two exchanging the battle's messages, and played to its
   end: in Free For All, the joining player forfeiting, the host winning.
   The same between two network namespaces, each with an address of
   its own, joined by a virtual link: as two machines on a network.
   Next: two physical machines, and a game won in battle.
8. **Importing** (done for the CDs): InstallShield 5's headers in
   `src/common/installshield.cpp` ([its page](../../../../common/installshield.md)),
   and the `MetaEngine`'s `planImport` and `addToImport`, from the two
   CDs: the import is byte for byte an install made by setup. The
   2.0.3.6 patch applied as it's imported, its download taken apart
   ([mscab.md](../../../../common/mscab.md)) and its own `Patch.exe`
   run on the game in the emulator ([systems/patching.md](systems/patching.md)).
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
| November 2026 | Hosting goes on, after the spike | The program loads, runs its start-up, reads its files and makes its window in Reluva's emulator, using the shared stand-ins with few additions; it runs at about 61 million instructions a second. What stops it, Direct3D 6, is a known piece of work on the shared Direct3D 7 that other games of 1998 to 2000 need too. Nothing was found that hosting can't do; how fast a mission needs to run is to be measured once one runs ([systems/hosting.md](systems/hosting.md)) |
| November 2026 | Star Fleet Battles' rules (the Cadet PDFs in the install, the published rulebooks) are a source for how combat works | Published by the board game's publisher; the game is built on its rules |
| November 2026 | The community's write-ups on the files are read for layouts only, each field checked against the files | As `botf` reads its community's editor: public documentation, no code copied |
