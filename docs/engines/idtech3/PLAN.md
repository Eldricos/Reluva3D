# The id Tech 3 engine: plan

## Goal

Every game built on the Quake III Arena engine, Elite Force first (see
[README.md](README.md#why-one-engine-for-a-family-of-games)).

## Phases

### Next steps

1. **Check against the retail data.** Done for maps (all 67 load) and
   detection (one release); the retail paks are read; Elite Force's
   shaders use no directive outside Quake III's; its map entities are
   surveyed ([games/stvef/formats/entities.md](games/stvef/formats/entities.md)).
   The Expansion Pack is detected too. Still to do: detect its other
   releases (the Mac and other-language versions).
2. **3D rendering in `System`.** Done: `gfx::Renderer3D` in
   `src/common/gfx3d.h` (textures, static meshes, draw calls with blend,
   depth, cull and alpha-test state, a 2D overlay), reached through
   `System::init3D`. The SDL3 backend implements it with SDL's GPU API
   (`src/backends/sdl3/sdl_gpu.cpp`) on the same GPU device as SDL's
   renderer, which the launcher keeps using. Shaders: see
   `src/backends/sdl3/shaders/build.sh`. Vulkan is tested: Mesa's software driver on Linux, and an
   NVIDIA RTX 2080 on Windows (Visual Studio 2022 build), where retail maps
   run at the monitor's 144 Hz with about 1.2 ms of CPU time per frame.
   The Metal path
   (macOS) is checked by continuous integration: `reluva-render-check`
   draws test frames through the renderer on a GitHub macOS runner and
   compares them with references made on Vulkan (`tests/render`,
   `.github/workflows/ci.yml`); it hasn't been played on a Mac yet.
3. **World renderer**, continued: autosprites, mirrors, culling (by the
   vis data, areas and the view) and drawing order are done, and fog volumes
   follow the original's depth model. Translucent surfaces are drawn as
   the original sorts them, by their shaders' sorts across the level,
   models and effects; like the original, not by distance.
   Checked against the original: retail maps look right at 144 fps on an
   RTX 2080, and Elite Force's 1872 shaders use no directive outside
   Quake III's; their autosprite2 fire and portal/mirror shaders are
   drawn too.
4. **Models**: done for MD3 and Elite Force's MDR, with interpolation
   between frames and the players' `animation.cfg` (the Models page plays
   them in pairs), and every level of detail. Players show in the Play
   page and the menus. Shadows are the client game's own marks
   (cg_shadows 1); still to do: the renderer's stencil and projected
   shadows (r_shadows 2 and 3, which Elite Force's menus don't offer).
5. **Bots**: done: they join, their library reads the `.aas` map with
   areas and routes, characters and the bots' input, and they choose
   goals and weapons, follow routes (lifts included), predict moves and
   chat ([aas.md](formats/aas.md)). They don't answer chat:
   the retail data has no reply file for them.
6. **Network play**: Elite Force's protocol 24, as the retail game patched to
   1.2 speaks it ([games/stvef/systems/network.md](games/stvef/systems/network.md)).
   The Play page opens each level it starts to other players on port
   27960 (or the next free of 27961-27963), when the game code is
   patch 1.2's: the protocol carries 1.2's layouts, and Reluva picks the
   game code's layout (1.0's or 1.2's) by the size of the player states
   it copies. `connect <address>` (the console, or the menus' server
   list, whose local search and pings work) joins another's server;
   `reconnect` and `disconnect` as the original. `reluva-tool idtech3 net-run`
   hosts a level with no player of its own, or joins one and reports
   what it's sent. Level changes on a Reluva host keep everyone, as the
   original's: the game's `map` (the end of a match, a vote) loads the
   level, starts the game code afresh and connects every player and bot
   again, those on other machines sent the new game state; `map_restart`
   (with a delay unless `g_doWarmup`, counted down in the warmup
   configstring) restarts the game code in place, a game type or player
   count change making it a full change; `vstr`, `kick`, `clientkick` and
   `set` are the engine's as well. Played Reluva to Reluva, and solo, on the retail data
   patched to 1.2 (which also confirms 1.2's layouts), and against the
   retail game both ways (October 2026). Reluva answers a pure server's
   check (`cp`, with pk3 checksums as the original's), tried against a
   pure retail server, and hosts pure levels itself (`sv_pure 1`, set by
   the menus' Pure Server line or `net-run -pure`), checking players'
   paks; the level's paks are listed as the original lists them. Snapshots hold only what
   the player could see, as the original's server picks it: not behind a
   closed door, in a cluster their eye's can see, unless sent to all;
   one-client and client-mask flags with 1.2's layout; through portal
   cameras too. The menus' Internet list asks the master servers
   (`sv_master1` to 5: Raven's, gone, then the community's
   `efmaster.tjps.eu`; Elite Force's master 0 asks every one), and a
   public server (`dedicated 2`, `reluva-tool idtech3 net-run -public`) sends them
   heartbeats. Rates as the original's: each player's `rate`, players on
   the local network unlimited, snapshots skipped (and the next flagged)
   when the last datagram hasn't had its time, messages in pieces sent a
   piece at a time. Downloads: a server with `sv_allowDownload 1` sends
   the paks its level uses (game code, map; never Raven's `pak0`-`pak3`)
   to players missing them, and a player with `cl_allowDownload 1`
   downloads them into the save folder's `baseEF` (mounted above the
   install's at start), checked against the server's checksum.
7. **Sound**: done in the Play page: sounds placed in the world, looping
   sounds and music, as the client game asks.
8. **Game code**: see below.

## Decisions

### Game code: open questions

The renderer and file formats are the shared, well-documented part. The
game logic is the hard part, and there are several possible routes. Each
one has to be checked before anything is written:

- **Licences.** Reluva's own code is all new. The GPL Quake III Arena
  source (id's 2005 release, ioquake3) may be read as documentation of the
  formats the games share, never copied.
- **Raven's Elite Force game source is off-limits.** The single-player
  game source Raven published in 2002 (the "STEF Game Source", expansion
  version) comes under Raven's *Limited Use Software License Agreement*,
  not the GPL. It allows only modifications that run with the retail game,
  and forbids derivative works, reverse engineering and translation. Don't
  copy it, and don't read it to write Reluva code; treat it like the leaked
  source CONTRIBUTING.md warns about. The same goes for the multiplayer SDK
  unless it turns out to carry a GPL-compatible licence. It also wouldn't
  help with the shared formats: by its own ReadMe it holds only the game
  DLLs' code, not the executable's renderer or map loading.
- **Holomatch (multiplayer)**: `pak0.pk3` holds `vm/qagame.qvm` (server
  game logic), `vm/cgame.qvm` (client: HUD, effects, prediction) and
  `vm/ui.qvm` (menus): game logic as Quake III VM bytecode. So the original
  logic can run in a QVM interpreter, with the engine providing the
  system calls it makes, the way ScummVM runs games' own scripts. The
  bytecode stays in the user's copy; Reluva only runs it.
  - The QVM file format and instruction set are the same as Quake III's,
    documented in the GPL source and community write-ups, so the
    interpreter is new code written from that documentation.
  - The system calls (numbers, arguments and meanings) are Elite Force's
    own. They have to be worked out from the bytecode in an owned copy
    (reverse engineering for interoperability, allowed by CONTRIBUTING.md
    where local law permits) and from Quake III's documented ones where
    they match, never from Raven's SDK.
  - Only Holomatch uses the QVMs. Single player's engine (`stvoy.exe`)
    loads its game and menus from the DLLs (`LoadLibrary` of
    `efgamex86.dll` and `efuix86`), whatever the paks hold.
- **Single player**: its logic ships as native x86 code
  (`efgamex86.dll`, with the menus in `efuix86.dll`), 32-bit Windows DLLs
  built with Visual C++ 6. Reluva runs them the way it runs the QVMs: in
  an interpreter, here of the x86 processor (`src/common/x86`, see
  [x86.md](../../common/x86/README.md)), with the engine's functions and the few Windows ones
  they call (KERNEL32's, for their C runtime) provided natively. The DLLs
  stay in the user's copy. Their interface to the engine (the tables
  `GetGameAPI` and `GetUIAPI` exchange) was worked out from the retail
  executable, with Jedi Outcast's GPL source, from the same studio and
  engine generation, as a guide to its shape: see
  [games/stvef/systems/sp-interface.md](games/stvef/systems/sp-interface.md).
