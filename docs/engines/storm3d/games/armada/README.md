# Star Trek: Armada

Game id `armada` (Activision, 2000), the first game on the
[Storm3D engine](../../README.md); its own code will go in a folder of
the engine's named `armada/`.

A real-time strategy game: four races (the Federation, the Klingons, the
Romulans and the Borg) build stations and fleets from dilithium, with
crews and officers, in a four-campaign story and in skirmish and
multiplayer games.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English retail 1.0 (`Armada.exe` of 10 March 2000) | `Armada.exe`, `assets.zfs` and `objtxt.zfs` by their hashes ([formats/install.md](formats/install.md)) | Recognised; nothing runs yet |
| Other releases (the 1.1 and 1.2 patches, other languages) | `Armada.exe`, `assets.zfs` and `objtxt.zfs` by their names, found as "unknown release" | Not seen: only the English 1.0 install has been |

## Where it stands

The English 1.0 install is recognised, and the game shows an empty
screen until Esc. Its ZFS archives can be listed and extracted with
`reluva-tool storm3d`, and its text files read: the object definitions
with their includes, the settings in C, the key and mouse bindings, and
the installer's encoded scripts ([formats/](#this-folder)). The data
viewer shows its pictures, models, sprites and fonts, and text, and plays
its sounds and movies. The original program runs in Reluva's emulator as
far as its first mission, as a reference to study
([systems/hosting.md](systems/hosting.md)). What comes next is in [PLAN.md](PLAN.md).

## Using it

- **The game:** add the install folder in the launcher (*Add Game...*).
  It shows an empty screen; Esc goes back to the launcher.
- **The data viewer:** `reluva-viewer` with the game. Tab moves between
  its pages, Esc leaves:
  - **Images:** Left/Right step through the pictures, PageUp/PageDown 10
    at a time, Home/End to the ends, F to the next folder, A shows the
    alpha over a checkerboard, I hides the status line.
  - **Models:** drawn in 3D, turning. Left/Right pick one, PageUp/PageDown
    10 at a time, Up/Down tilt, +/- nearer and further, Space stops or
    starts the turning, L the next level of detail, less detailed.
  - **Sprites:** Left/Right pick one, PageUp/PageDown 10 at a time,
    Up/Down step through its animation's keyframes, T to the next table.
    A font's sprite writes a line of text in its characters.
  - **Sounds:** Up/Down pick one, PageUp/PageDown 20 at a time, Space
    plays or stops it, A plays each as it's picked.
  - **Movies:** Left/Right pick one, Space plays or pauses it, Home goes
    back to its start. The opening movie is on the CD: copy
    `setup/animations/STIntro.bik` into the install's `animations/` to see
    it.
  - **Text:** Left/Right pick a file, F to the next kind, Up/Down and
    PageUp/PageDown scroll, Home/End to its start or end.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | The game's own files, a page each: [install.md](formats/install.md) (what the install and the CD hold), [zfs.md](formats/zfs.md) (the archives), [odf.md](formats/odf.md) (object definitions), [config.md](formats/config.md) (settings in C), [ldl.md](formats/ldl.md) (the installer's scripts), [input-map.md](formats/input-map.md) (the keys and mouse), [textures.md](formats/textures.md) (textures and pictures), [sounds.md](formats/sounds.md), [movies.md](formats/movies.md), [spr.md](formats/spr.md) (sprite tables and fonts), [sod.md](formats/sod.md) (models), [bzn.md](formats/bzn.md) (maps) |
| [systems/](systems/) | How the game behaves, a page per part: [hosting.md](systems/hosting.md) (the original run in the emulator, as a reference) |
| [research/](research/README.md) | Working notes: the missions' API, the program's classes, its start-up traced |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **Mission DLLs:** the campaign's missions, each a pair of Windows DLLs,
  `<name>S.dsl` and `<name>R.drl` ([formats/install.md](formats/install.md#missions)).
- **AIP:** an "Artificial Intelligence Personality", a text file of the
  computer player's priorities (`AI/AIPs/*.aip`).
- **Cineractive:** the word the game's exports use (`CineractiveShot`) for what looks like a scripted cut scene in the game's own 3D *(inferred)*.
