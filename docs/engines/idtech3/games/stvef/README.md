# Star Trek: Voyager - Elite Force

Game id `stvef`, on the [id Tech 3 engine](../../README.md); its own code
is in `src/engines/idtech3/eliteforce/`. The engine's file formats, which
Elite Force shares with every Quake III engine game, are in the engine's
[formats/](../../formats/); where the engine stands with this game is on
the [engine's page](../../README.md#where-it-stands).

## Where it stands

How far Reluva has got with Elite Force, part by part. The
[engine's page](../../README.md#where-it-stands) sums each part up in a line.

| Part | State |
|---|---|
| Detection | Elite Force: the Windows CD's pak0.pk3 by its hash (no Windows program needed); other releases by file names, shown as "unknown release". The description gives the patch level (pak1.pk3: 1.1, pak2.pk3: 1.2, by their hashes) and the Expansion Pack (its pak3.pk3, by its hash), says when single player's DLLs are missing, and is brought up to date when the game is started. The Expansion Pack's DLLs (told from 1.0's and 1.1's by their hashes) are its own single player's: an import keeps them in `expansion/`, beside the root's 1.1 DLLs, so a game can be played either way; an install made with the Expansion has only its DLLs, in the root, and then single player is the Expansion's alone. Each game in the launcher has a *Use the Expansion Pack* setting (on by default; `expansion=0` in its section of `reluva.ini`): off, pak3.pk3 isn't loaded and single player uses the 1.1 DLLs. Adding the same folder twice gives two entries, one with it on and one off |
| Import | From the English Windows CD (`SETUP/BaseEF/pak0.pk3` and the DLLs beside it are plain files on it), an install, or a copy of either: the game folder's `.pk3` files, CD key files (`efq3.key`, `expefq3.key`), saved games and demos, and `efgamex86.dll`/`efuix86.dll`, into a folder of Reluva's (`<data>/library/stvef`). From the CD alone that's 1.0; with the 1.2 patch's installer (`eliteforcepatch1_2.exe`, picked with *Add patch...* or found beside the folder picked) its `pak1.pk3`, `pak2.pk3` and 1.1 DLLs are taken out of it as the rest is copied, giving 1.2 (`docs/common/wise.md`). *Apply Patch...* does the same for a game already added, into its folder. The Expansion Pack comes from its CD (its `Setup/baseef/pak3.pk3`, with pak1/pak2, which are 1.2's, and the DLLs in `Setup/`) or an install of it: *Add expansion...* in the import, or *Add Expansion...* for a game already added, copies pak3.pk3, `expefq3.key` if there is one, and its DLLs into `expansion/`. The expansion's single player was changed (its own `GetGameAPI` table, Init and ReadLevel; see [systems/sp-interface.md](systems/sp-interface.md)), which Reluva tells by the DLL's hash. Its saved games can't be loaded by 1.x's game code, nor 1.x's by it (below), so Reluva keeps them apart: 1.x's in the game's save folder's `saves/`, the Expansion's in `expansion/saves/`; saves found in the other's folder are moved to their own as single player starts, and the load menu leaves out those of the other version (the game folder's own included), a load of one refused with a message |
| Mods | As Elite Force's engines: a mod is a folder beside `baseEF` with paks of its own; `fs_game` (flags system info and init) names the one played. Its folder (in the install, then in the save folder) is searched above `baseEF`'s, so its paks, loose files and (in Holomatch) game code (`vm/*.qvm`) win; single player's DLLs stay the install's, as the original loads them from the game's folder whatever the mod. The launcher's *Mod* setting for each game picks one, as the original's `+set fs_game`; the menus' Mods screens set `fs_game` and run `vid_restart`, and Reluva then mounts the game's files again and starts the pages over (Holomatch's engine does this, FS_ConditionalRestart at 0x421bf0 in `stvoyHM.exe`; single player's only reads `fs_game` as it starts, Reluva switches at once there too). Joining a server whose system info names another mod switches to it and joins again, as the original's client. A mod's saved games, settings and screenshots go in the game's save folder's `mods/<mod>/` (its settings start as the game's until it has its own); the CD key is shared; paks downloaded for it go in the save folder's `<mod>/` |
| File system | done: `baseEF` loose files over its paks, later paks first (`filesystem.cpp`, [pk3.md](../../formats/pk3.md)) |
| Text files | done: tokenizer ([text.md](../../formats/text.md)) and entity lump |
| Shader scripts | every Quake III directive parsed into typed fields, unknown ones listed (`reluva-tool idtech3 shader-check`) ([shader.md](../../formats/shader.md)) |
| Maps | every BSP lump read and validated; all 67 retail maps load ([bsp.md](../../formats/bsp.md)) |
| Images | TGA, JPEG, PNG (`image/image.cpp`) |
| Models | MD3 with every frame, tags, `.skin` files, players put together from their parts, lit by the light grid in maps ([md3.md](../../formats/md3.md)); placed where entities name a model; Elite Force's MDR models with their levels of detail; items and characters as the game code places them |
| Game code | QVM interpreter done and tested against compiler output ([qvm.md](../../formats/qvm.md)). Elite Force's system calls worked out from its own code ([systems/syscalls.md](systems/syscalls.md)); the menus' calls implemented (`eliteforce/ui_host.cpp`, `reluva-tool idtech3 ui-run`): console variables, files, registering, drawing. The Menu page runs them and draws them with the GPU renderer, with the keyboard and mouse; on the retail game the setup, CD key and main menus work, with their 3D models (the player in Player settings): the scenes both programs hand over are recorded by `client/scene.cpp` and drawn by `client/scene_drawer.cpp`, each RenderScene over the pictures drawn before it with depths cleared, those without the level lit evenly from above as the original lights them. While the client game loads, its UpdateScreen calls draw its loading screen (its frame drawn in the middle of its start-up, as the original does). qagame starts a level (`eliteforce/game_host.cpp`, `reluva-tool idtech3 game-run`): its entities spawn and link, with collision against the map (`collision.cpp`, curved patches included; areas as the original's: an entity's one or two areas found when it's linked, the game opening and closing the portal a door stands in (AdjustAreaPortalState), InPvs not seeing through a closed one, AreasConnected, the original's cm_noAreas to join them all; the Play page's info line counts the sight checks a closed door blocked; snapshots leave out what's behind closed doors and out of sight, see Network play below), and a player joins and moves. The client game (`eliteforce/cgame_host.cpp`) takes snapshots and the player's commands, draws the HUD and builds the 3D scene. The Play page runs both each frame with the keyboard and mouse (through key bindings: the game's `default.cfg`, changed in the menus' Controls and kept in `reluva.cfg` in the save folder; `bindings.cpp`), and draws the level from the player's eye with the HUD over it. The engine's settings from the menus apply: brightness (r_gamma, a final pass on the GPU raising each colour to 1/gamma, as the original's hardware gamma), mouse speed and inversion (sensitivity, m_pitch, m_yaw), smooth mouse (m_filter), free look (cl_freelook), key turn speeds (cl_yawspeed, cl_pitchspeed, cl_anglespeedkey), always run (cl_run), dynamic lights (r_dynamiclight) and screen size (the client game's view rectangle). The screenshot command (and screenshotjpeg, also as a TGA) saves the next frame, without Reluva's info lines, as `screenshots/shot0000.tga` (the first number free) or the name given, in the save folder; `screenshot silent` says nothing, as the original. Sounds play through `sound_system.cpp` as the client game asks (calls 28 to 35, Quake III's): placed in the world or following entities, full volume within 80 units then fading by 0.0008 a unit, panned by the listener's right; no sound cutting another (as ioq3's S_StartSound: the same sound on the same entity within 50 ms is skipped as a repeat; ioq3's cap of 5 copies of one sound per entity is left out, as it silenced rapid fire); looping sounds re-added each frame; the music's intro then its loop; at s_volume and s_musicvolume. The video settings apply too (`videoSettings()` in `pages/page.cpp`, read when a page is shown, and again when the menus run vid_restart, which starts the menus over as the original does; the engine's settings are registered as kept, so they're saved in `reluva.cfg`): resolution (r_mode, Quake III's list of sizes, or r_customwidth/height for -1; the frame is drawn at that size and letterboxed in the window, which takes that size when it fits on the screen), fullscreen (r_fullscreen, at the desktop's mode), texture detail (r_picmip: textures halved that many times with a 2x2 box filter, but for shaders marked nopicmip), texture filter (r_texturemode, GL_ names; r_ext_texture_filter_anisotropic, 16x), neither applying to pictures (menus, HUD), which the original loads without mipmaps, lighting (r_vertexLight: the level lit by its vertex colours instead of lightmaps), geometric detail (r_subdivisions: curves drawn straying at most that many units from round, as the original's, and bullet marks on them following; r_lodBias: models' `_1`/`_2` lower details, and MDR models' own levels of detail; and lower levels with distance, as the original chooses them) and simple sky (r_fastsky: the sky left black). |
| Viewer | Menu (the game's own menus), Play (a Holomatch level run by the game's own code; Esc shows the game's own in-game menu over the match, as the original: the menus started on their menu 2, told they're connected (CA_ACTIVE, the player's number) and given the match's configstrings, their commands run at the Play page's console (`cmd ...` sent as the player's), closing when they let go of the keys; their Setup's `vid_restart` restarts the renderer, the level's graphics and the client game while the server and connection go on, as the original's; leaving a match brings the menus back to their main menu), World (3D, with entities' models), Models, Maps (wireframe), Textures and Files pages |
| Renderer | every shader stage with its blend, depth, alpha test, rgbGen/alphaGen, tcGen/tcMod and animMap; deformVertexes wave/bulge/move/normal (`normal` as the original's: each normal pushed by its amplitude times a 4D value noise of the position and time, for the CTF flags' shimmer; of the rest, Elite Force's shaders (1,891 in 1.2, 2,059 with the Expansion Pack) use only `projectionShadow`, the original's shader for r_shadows 3, not drawn); sort order (as the original's: everything in a view, the level's surfaces, models and effects alike, drawn by its shader's sort, the solid ones first with the dynamic lights over them); sky boxes and cloud layers; curved surfaces cut and stitched as the original's ([bsp.md](../../formats/bsp.md)), always at full detail; fog volumes (as the original's: by depth along the view, thinned towards the volume's surface when the eye is outside it); the original's implicit shaders and light scaling ([shader.md](../../formats/shader.md)). Dynamic lights. Models' levels of detail as the original chooses them (each model's radius as seen, then r_lodbias). Visibility as the original's renderer: in the Play page only the level's surfaces in leaves whose cluster the eye's can see (the vis data) and whose area a closed door doesn't hide (the snapshot's area mask, which the server works out from the player's area and the open doors, handed back in the view at refdef_t's byte 80) are drawn, worked out again when the cluster or the mask changes; then, each frame the view moves, only those whose bounds are inside the view's four sides (from its matrix) go in the frame's index list, uploaded into a reused, cycled GPU buffer. Mirrors and portals as the original's renderer: the nearest mirror or portal surface in view with a portal entity (type 8 in Holomatch, Quake III's portal surface one on; 7, Quake III's own, in single player) within 64 units of its plane, a portal within its shader's alphaGen portal range; its camera the mirror's reflection or the portal's (old origin, axes, turning by frame, old frame and skin number); that view drawn first inside the surface's shape (marked in the stencil), clipped at its plane (an oblique near plane), turned inside out for a mirror, with its own culling; then the surface's depth and the main view over it, the portal's shader blending over. The player's body is seen in mirrors, their weapon isn't. Autosprites as the original's: the level's autosprite surfaces kept aside and rebuilt as drawn, each quad facing the camera round its middle (autosprite) or turned about its long axis (autosprite2, the fire shaders), in mirrors too; not yet on models |
| Sound | sounds by name, decoded once (`sounds.cpp`), mixed by `src/audio`'s mixer; the menus' sounds; in the Play page sounds placed in the world, looping sounds and music (`sound_system.cpp`) |

## Using it

You need the game: the English Windows CD (or a disc image), or an install
made by its own setup. The 1.2 patch installer (`eliteforcepatch1_2.exe`)
and the Expansion Pack's CD are optional.

- **Adding the game.**
  - *Add Game...* in the launcher plays it from an existing install.
  - *Import Game...* copies it from the CD (or an install) into Reluva's
    library folder. It takes only the files Reluva needs, so nothing has to
    be installed, on any system.
- **The patch.** *Add patch...* during an import, or *Apply Patch...* for a
  game already added, takes 1.2's files out of the patch installer without
  running it.
- **The Expansion Pack.** *Add expansion...* during an import, or *Add
  Expansion...* for a game already added, copies it from its CD. Each game
  has a *Use the Expansion Pack* setting, so the same folder can be added
  twice to play with and without it.
- **Mods** (folders beside `baseEF` with their own paks) are chosen with a
  game's *Mod* setting, or from the game's own Mods screens.
- **From a terminal:** `reluva-tool detect <folder>` and `reluva-tool import
  <source> [destination] [-patch <installer or Expansion CD>]...` do the
  same as the launcher.

A game starts on a page to choose single player, Holomatch or the
developer tools:

- **Single player and Holomatch** are the game's own menus and play, with
  its own keys.
- **The developer tools** are pages for the game's data. **Ctrl+Tab** steps
  through them; plain **Tab** is the game's.
  - **3D world:**
    - **Left/Right** picks a map; drag to look;
    - **W/A/S/D** move, **Space/C** (or **E/Q**) go up and down, **Shift**
      goes faster;
    - **R** returns to the start, and **I** shows or hides the info.

    Models placed by the map's entities show too.
  - **Models**, players put together:
    - **Left/Right** step through them; drag to turn, and the wheel zooms;
    - **A** animates, and **,/.** step frames.
  - **Wireframe maps:**
    - **Left/Right** picks one;
    - drag (or **Shift+arrows**) rotates, the wheel (or **+/-**) zooms, and
      **W/A/S/D/Q/E** move;
    - **P** shows or hides patches, **I** the info, and **R** resets the
      view.
  - **Textures:** **Left/Right** step through them (**PageUp/PageDown** by
    10).
  - **Files:** the search path, and which archive each file comes from.

3D needs a GPU with Vulkan (Linux, Windows) or Metal (macOS).

## This folder

| Doc | What's in it |
|---|---|
| This page | Using it: adding the game, the patch, the Expansion Pack, mods, and the pages |
| [formats/](formats/) | [Map entities](formats/entities.md), as Elite Force's maps use them, and [the 1.2 patch](formats/patch.md) |
| [systems/](systems/) | Its game code's interfaces ([single player's DLLs](systems/sp-interface.md), [the QVMs' system calls](systems/syscalls.md)), [the network protocol](systems/network.md), [joystick input](systems/input.md), [running single player's DLLs](systems/single-player.md) and [cinematics](systems/cinematics.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva deliberately differs from the original game |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What's assumed rather than confirmed |
| [TESTING.md](TESTING.md) | What still needs playing |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was all worked out, and the rules followed |

## Terms

Elite Force's own words these pages use. The engine's (QVM, cgame,
qagame, configstring, cvar and the rest) are in the
[engine's Terms](../../README.md#terms).

- **Holomatch:** Elite Force's multiplayer: matches against players over a
  network or bots, in its own program (`stvoyHM.exe`), its code in QVMs.
  Single player is the story, in `stvoy.exe`, its code in DLLs.
- **ICARUS:** the script system Raven's single-player games share. The
  story's scripts are compiled to `.IBI` files, which the game runs as
  maps' entities ask ([entities.md](formats/entities.md);
  `reluva-tool idtech3 ibi-dump` prints one).
