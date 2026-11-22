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
`reluva-tool storm3d` ([formats/zfs.md](formats/zfs.md)), its object
definitions read with their includes ([formats/odf.md](formats/odf.md)),
and its settings files ([formats/config.md](formats/config.md)). What comes next
is in [PLAN.md](PLAN.md).

## Using it

Add the game's install folder in the launcher (*Add Game...*). The game
shows an empty screen; Esc goes back to the launcher.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| [formats/](formats/) | The game's own files, a page each: [install.md](formats/install.md) (what the install and the CD hold), [zfs.md](formats/zfs.md) (the archives), [odf.md](formats/odf.md) (object definitions), [config.md](formats/config.md) (settings in C) |
| `systems/` | How the game behaves, a page per part |
| `research/` | Working notes |
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
