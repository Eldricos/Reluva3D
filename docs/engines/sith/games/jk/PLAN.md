# Jedi Knight: plan

## Goal

The US retail release played from its own files: the 21 single-player
levels with their cut scenes, the menus, saves, multiplayer and Capture
the Flag. Then Mysteries of the Sith on the same engine, once a copy is
available.

The copy studied, and what each file holds, is in the
[README](README.md#the-install-and-the-cds).

### Two ways to play the game

**A. Native.** Rebuild the Sith engine in Reluva: levels (sectors and
portals), models and keyframes, the COG script machine and its verbs,
physics and collision, AI from the `.ai` files, the HUD and menus,
saves, multiplayer.

- For: portable, moddable, a renderer of Reluva's own.
- Against: physics, collision, the AI and the COG verbs have to be
  matched from `JK.EXE` and from watching the game.

**B. Hosted.** Run the retail `JK.EXE` in the x86 emulator
([docs/common/x86/](../../../../common/x86/README.md)), as NFS III and LEGO
Alpha Team are run. It imports DirectDraw, DirectSound, DirectPlay,
DirectInput and `smackw32.dll`, and names WINMM's `cdaudio` device, for
its music on the CDs' audio tracks *(inferred)*. The
emulator has DirectDraw, Direct3D 7 and DirectInput, but not the older
Direct3D interfaces Jedi Knight's DirectX 5 era used, DirectSound or
DirectPlay; each would be new shared code.

- For: the game exactly, sooner.
- Against: the new stand-ins, and the CD audio, which a copy of the CD's
  files doesn't hold.

**Recommendation:** the data layer and viewer first (needed either way),
then a hosting spike, and a choice at a gate. Much of Jedi Knight is text
data and COG scripts, so native is more within reach than for NFS III;
hosting stays the faster way to a game that plays as the original, and
the reference to check native against.

### The gate

What the trial found, for choosing between A and B:

- **B's cost is known and bounded.** What `JK.EXE` needs that the
  emulator hasn't is DirectX 5's: Direct3D 1 and 2 (execute buffers,
  viewports, materials, older textures) on the emulator's Direct3D 7
  rasterizer; DirectSound with 3D buffers on Reluva's mixer; about 75
  small Windows calls; later DirectPlay 3 for multiplayer, and the CDs'
  music. All of it is shared code, of use to any DirectX 5 game.
- **Speed isn't a concern.** 200 to 500 million instructions a second,
  for a program written for the Pentiums of 1997.
- **A's cost is the whole engine.** The data is read and drawn already,
  but the game's rules are in `JK.EXE`: the COG script machine and its
  verbs, physics and collision, the AI's instincts, weapons and the
  Force, the menus, saves and multiplayer, each to be matched by reading
  the code and watching the game.

**Recommendation: B**, with the data layer and viewer kept as they are
(the reference to check the hosted game against, and the start of A if
it's ever wanted). Next: Direct3D 1 in the emulator, until `JK.EXE`
reaches its menus; then DirectSound; then a level.

## Phases

1. **Skeleton and detection** (done): the engine from the template, the
   family layout, the US release recognised, the CDs found
   ([formats/jk-cd.md](formats/jk-cd.md)), and *Import Game...* from them
   ([README.md](README.md#using-it)).
2. **Archives and pictures** (done): the `.gob` archives, `.cmp`
   colormaps, `.bm` pictures, `.mat` materials, `.sft` fonts and `.uni`
   string tables ([formats/](README.md#this-folder)), each read on every
   retail file and shown on a page of the data viewer.
3. **Sound and movies** (done): the `.wav` sounds, `.snd` sound classes,
   `episode.jk` episodes, and the Smacker cut scenes from both CDs, each
   read or decoded on every retail file and shown or played by the data
   viewer.
4. **Models and levels** (done): `.3do` models, `.key` animations, `.pup`
   puppets, `.spr` sprites, `.par` particles, and the `.jkl` levels with
   their things, each read on every retail file and drawn in 3D by the
   data viewer ([formats/](README.md#this-folder)).
5. **Hosting trial** (done): `JK.EXE` run in the emulator gets through its
   start-up (its registry, window, archives, strings, fonts and the menus'
   pictures) and stops where it asks for Direct3D's first interface,
   `IDirect3D`, which the emulator hasn't; it runs at 200 to 500 million
   instructions a second ([systems/hosting.md](systems/hosting.md)). See
   [The gate](#the-gate).
6. **The game** (under way), hosted, as the gate chose: `JK.EXE` plays
   in Reluva's window, its opening, menus, cut scenes and levels drawn by
   its own software renderer or through Direct3D 1, with the keyboard
   and mouse, its sound through DirectSound, the CDs' music through MCI,
   each CD put in the drive as it asks for it, and multiplayer over
   TCP/IP between copies of Reluva
   ([systems/hosting.md](systems/hosting.md)). Every level, cut scene
   and both endings have been gone through with the game's cheats
   ([TESTING.md](TESTING.md#last-checked)). Next: levels played through
   by a person, and the multiplayer games at length, to find what
   differs; then Mysteries of the Sith.
7. **Mysteries of the Sith.**

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | Engine id `sith`, game id `jk`, the layout for a family of games | It's the Sith engine, and Mysteries of the Sith runs on it too |
| November 2026 | Detection keys on `JK.EXE` and two archives in the install; a CD isn't taken for an install | The executable tells releases apart; the archives are the same in the install and on the CDs, and the game runs from an install |
| November 2026 | The CDs are told apart by `JK_.CD`, not by their folders' names | Copies of CDs get any name; the marker is what differs between them |
| November 2026 | An import copies the CDs' cut scenes into the install, under `cd1/` and `cd2/` with the CDs' own paths | The game reads them from the CDs; kept with the CDs' paths and markers, the same code finds them there as beside the install |
| November 2026 | The data layer and viewer before choosing native or hosted | Needed either way |
| November 2026 | Published write-ups of the formats by the editing community are used for facts only, each checked on the game's files | Allowed by CONTRIBUTING.md, as for Birth of the Federation's editor |
| November 2026 | Decompilation-based rebuilds of the game (OpenJKDF2 and the like) aren't read, for facts or code | Their code is a function-for-function rebuild of `JK.EXE`'s; reading it would make copying hard to rule out. `JK.EXE` is studied directly instead |
| November 2026 | Way B, hosting `JK.EXE`, chosen at the gate; the data layer and viewer kept | The trial showed B's cost bounded and shared with other DirectX 5 games, where A's is the whole engine ([The gate](#the-gate)) |
| November 2026 | Played before it had sound: the game was told there's no sound card, joystick or CD audio, as Windows would without them | It plays to the end that way, and each device can be added on its own later; DirectSound and the CD's audio now are |
| November 2026 | The CD the game asks for is put in the drive as its check (`0x40ebb0`) starts, by a jump there to engine code; the game never asks to change CDs | The game can't be asked to see a CD that isn't there, and a player has nothing to swap: both CDs' files are on the disk. Hooking the check, where the CD's number is an argument, is exact; the marker that passes for any CD would have hidden which CD's music to play |
| November 2026 | The CDs' music from their audio tracks beside each CD's files: a cue sheet and its image, read as it plays, or tracks ripped as `trackNN.wav` or `.mp3` | A copy of a CD's files doesn't hold its audio; an image's cue sheet needs no ripping, and ripped tracks are Reluva's way for other games' CD music (Clue) |
| November 2026 | The window shows the system's pointer while the game shows Windows', and holds the mouse while the game holds it through DirectInput | The menus use Windows' pointer without drawing one; play reads the mouse's movement |
| November 2026 | What the game writes to its registry is kept in the save folder (`registry.txt`), and given back each run | *Setup*'s choices, Direct3D's device among them, live in the registry; Windows kept them, and the emulator's registry is made afresh each run |
| November 2026 | Multiplayer through the emulator's DirectPlay, over a protocol of Reluva's own on UDP, between copies of Reluva; TCP/IP only | The game uses DirectPlay alone for its network, and only what any DirectPlay game would; Microsoft's protocol isn't published, and copies of Reluva meeting each other is what players need. UDP lets the game's unguaranteed messages be lost as they were, and runs on Reluva's loopback network for tests |
| November 2026 | A host to join beyond the local network is the `joinaddress` setting | Windows' TCP/IP provider asked for it in a box of its own, which the game never draws |
| November 2026 | The joystick through WINMM's joystick functions, from the player's game controller, its layout Reluva's; the game's own *Disable joystick* left as it is | The game reads joysticks only through WINMM; disabling it for a new player is the game's own choice, which its *Setup* undoes |
