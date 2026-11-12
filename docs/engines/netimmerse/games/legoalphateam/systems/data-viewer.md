# LEGO Alpha Team: detection, the archives and the data viewer

> **Status:** done · **Code:** `src/engines/netimmerse/viewers.h`, `src/engines/netimmerse/pac_archive.cpp` · **Checked against:** the retail files · **Confidence:** confirmed unless marked

## Overview

How Reluva recognises the game and reads its archives, and its data
viewer of the game's files (F12 in the game, or the engine without
`LEGOATeam.exe`), which the original doesn't have.

## How it works

The original reads the same files through its own code: the `.pac`
archives through `DDPackedDirFileLib.dll` ([pac.md](../formats/pac.md)),
and each kind of file as its page in [formats/](../formats/) says.

## In Reluva

### Detection and the archives

- Done: detection by `Locale_en.pac`'s hash, with a fallback by file names
  for other releases. The launcher finds the game. Starting it opens every
  archive and shows what each holds.
- Done: the `.pac` format ([pac.md](../formats/pac.md)), checked
  against all eight retail archives.
- Done: `PacArchive : Archive` (`src/engines/netimmerse/pac_archive.cpp`), so
  `.pac` files join a `SearchSet` like any other archive. It's tested in
  `tests/engines/netimmerse/unit/formats.cpp` (`netimmerse.unit.formats`), on archives the tests build.
- Done: `reluva-tool netimmerse pac-list` and `pac-extract`, as for New Worlds' `.cat`.
  On all eight archives they give the same 2,462 files, byte for byte, as
  a separate script written from the format notes.
- Done: what the archives hold ([pac.md](../formats/pac.md)):
  sounds as headerless PCM (`.DDS`), models (`.FIN`), TGA textures and
  lightmaps, puzzles, fonts and text.

### The data viewer

Like New Worlds and Elite Force before they ran, the engine first opens
as a viewer; it checks each reader against all the retail files. Started
from the launcher, the engine now shows these pages (keys in
`src/engines/netimmerse/viewers.h`; Tab moves between them):

- Done: **Archives**: what each `.pac` holds.
- Done: **Models**: every `.FIN` model, characters, parts and levels alike,
  drawn with the GPU renderer as its scene graph places its shapes. It has
  textures, lightmaps (a second pass multiplied over the first), vertex
  colours, materials and alpha blending, and the nearest level of
  level-of-detail nodes. The format is in
  [../formats/fin.md](../formats/fin.md);
  `reluva-tool netimmerse fin-check` reads all 147 models. Every object of the 21
  classes read ends exactly where the next begins, and every link and image
  name resolves.
  - **Characters.** Their limbs are skins that follow bones; all 15 are
    whole.
  - **Billboards** face the camera.
  - **Drawing state, as Direct3D's defaults:**
    - back faces culled, since nothing in the files asks for two-sided
      drawing and the stored normals agree with the triangles' winding;
    - depth written unless an `NiZBufferProperty` says otherwise;
    - shapes that blend drawn after the rest, the farthest first.

    Before this, blended closed shapes showed their insides: the
    skeletons' heads, whose skull texture blends, looked as if their
    normals were wrong.
  - **Hidden objects** (effects the game shows on events) and the
    characters' far-away stand-ins are left out unless H shows them.
  - **Animation** plays, from the game's `DDActorSharedData`: each 3ds
    node's position, rotation (quaternions or Euler angles), scale and
    visibility keys, which move the skins' bones. C and V pick a clip
    (`at_rest`, `pace right`, `win`... or the whole timeline), and Space
    pauses. Between keys, each follows its curve as the game does: Bezier
    and TCB positions by their cubic, rotations by squad, numbers by
    Hermite's, Euler angles as Z × Y × X. This was checked against the
    running game's bones in memory (see fin.md).
  - **Morph shapes** (the E3xxx levels' seaweed) move by their keyed
    weights, each on Hermite's curve as `NiBaryMorphKey` gives it.
  - Playing a clip moves only what the clip lists, as the game does:
    its tracks, flip-books and morph shapes. The rest stay where they
    were left (the whole timeline moves everything).
  - **Material animators** change colour and alpha.
  - **Flip-book textures** cycle their images as the game's
    `NiFlipTextures::Act` does, at the clip's time: those the clip lists,
    or all of them on the whole timeline. The others keep their image.
  - **Glows** (`DDCorona`) are drawn facing the camera, added to what's
    behind them.
  - **Lights**: G marks them. Pressed again, G lights the shapes with
    normals by them, as NiDirect3d's software lighting does: the dimmer,
    the light's type, attenuation by its distance and curve, and a spot's
    cone (fin.md). No file has a spot, so the cone hasn't been seen.
  - The fields these rest on, formerly guesses, were named from the
    game's own DLLs: their exported accessors and loaders, and the code
    that uses them (fin.md, "Checked against the game's code").
  - **Clip sounds**: a clip with a sound plays it as it starts, at its
    volume (looping if the clip's sound loops).
  - **Footprints**: F shows the square a part fills on the board, from
    its actor data.
  - The formats are in
    [../formats/fin.md](../formats/fin.md#animation).
    `fin-check` confirms that all 102 animated models have one track per
    3ds node, and that 1,027 of 1,035 first keys match the saved pose.
- Done: **Textures**: all 1,609 TGA images in `Map.pac`, `LMap.pac` and
  `Etc.pac`. All decode with `common/image`, 479 of them with alpha.
- Done: **Sounds**: all 498 `.DDS` sounds (`decodeDds`), played through the
  mixer.
- Done: **Text**: the language's text files and the puzzles.
  `parseLocaleText` reads the `==key==` format for later use.
- Done: **Fonts**: all 12 of the game's own (`.DDF` with their TGA
  pages), each with a sample, every character it has, and its pages (P).
  The format is in [../formats/ddf.md](../formats/ddf.md).
- Done: **Intro movie** (`lmiintro.avi`, CD only): Indeo 5 video with PCM
  sound. Reluva has its own AVI reader and Indeo 5 decoder; all 1,631
  pictures match FFmpeg's decode byte for byte. If the file is copied
  beside the install, the engine plays it before the game, and a key or
  click skips it. See [../../../../../common/video/avi.md](../../../../../common/video/avi.md)
  and [../../../../../common/video/indeo5.md](../../../../../common/video/indeo5.md), which
  record what came from FFmpeg's LGPL decoder.

## Sources and checks

The viewer was checked in this container with a software stand-in for the
renderer, since it has no GPU: frames of characters, parts and levels
looked as expected. Sound playback wasn't heard, as the container has no
audio device.
