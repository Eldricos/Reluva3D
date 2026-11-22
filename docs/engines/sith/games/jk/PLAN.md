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

## Phases

1. **Skeleton and detection** (in progress): the engine from the
   template, the family layout, the US release recognised, the CDs found
   ([formats/jk-cd.md](formats/jk-cd.md)). Next in it: *Import Game...*
   from the CDs.
2. **Archives and pictures:** GOB as an `Archive`, `.cmp` colormaps,
   `.bm` and `.mat` pictures, `.sft` fonts, `.uni` strings, each on a
   viewer page.
3. **Sound and movies:** the `.wav` files, `.snd` sound classes, the
   Smacker cut scenes from both CDs.
4. **Models and levels:** `.3do` models, `.key` animations, `.pup`
   puppets, `.spr` sprites, `.par` particles; `.jkl` levels drawn in 3D.
5. **Hosting spike:** how far `JK.EXE` gets in the emulator, and what it
   needs; the gate between A and B.
6. **The game,** by the gate's choice.
7. **Mysteries of the Sith.**

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | Engine id `sith`, game id `jk`, the layout for a family of games | It's the Sith engine, and Mysteries of the Sith runs on it too |
| November 2026 | Detection keys on `JK.EXE` and two archives in the install; a CD isn't taken for an install | The executable tells releases apart; the archives are the same in the install and on the CDs, and the game runs from an install |
| November 2026 | The CDs are told apart by `JK_.CD`, not by their folders' names | Copies of CDs get any name; the marker is what differs between them |
| November 2026 | The data layer and viewer before choosing native or hosted | Needed either way |
| November 2026 | Published write-ups of the formats by the editing community are used for facts only, each checked on the game's files | Allowed by CONTRIBUTING.md, as for Birth of the Federation's editor |
| November 2026 | Decompilation-based rebuilds of the game (OpenJKDF2 and the like) aren't read, for facts or code | Their code is a function-for-function rebuild of `JK.EXE`'s; reading it would make copying hard to rule out. `JK.EXE` is studied directly instead |
