# Star Wars Jedi Knight II: Jedi Outcast

Game id `jk2`, on the [id Tech 3 engine](../../README.md); its own code
is in `src/engines/idtech3/jk2/`. Raven Software's, 2002: Quake III's
engine as Raven changed it, a generation after Elite Force's.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English, Windows, 1.02a | `base/assets0.pk3`, 534,984,353 bytes, `54693e39dc8c122c8ba724664a584b60` (MD5 of its first 5,000 bytes, as detection hashes) | Recognised and imported; not playable yet |

Other releases are found by `base/assets0.pk3`'s name alone, as "unknown
release".

The same release's files, by the MD5 of the whole file:

| File | MD5 | Bytes |
|---|---|---|
| `base/assets0.pk3` | `bbd8243669d551c910cfe60ccb4e5edd` | 534,984,353 |
| `base/assets1.pk3` | `e91efd51a8883b1c2590e8be7705877f` | 86,950,705 |
| `jk2sp.exe` (single player; says `JK2: v1.02a`) | `494697528df318aac065f5b024a4ef47` | 999,482 |
| `jk2mp.exe` (multiplayer; says `JK2MP: v1.02a`) | `3dbfb179aa803eb7f7a0912556361723` | 1,126,400 |
| `jk2gamex86.dll` (single player's game code; `8ec27068a054ceac7861fd648561bddb` by its first 5,000 bytes) | `acc8fc1cea330d1a0ac0646d3bd96fad` | 1,318,912 |

`version.inf` beside them says version 1.02a for both. On the CD the game
is in `GAMEDATA/GameData/` as plain files, the same files an install has;
the cabinets beside it hold only the installer's own files.

## Where it stands

| Part | State |
|---|---|
| Detection | The English 1.02a release by `assets0.pk3`'s hash; others by its name, as "unknown release" |
| Import | From the CD (`GAMEDATA/GameData/`), an install (`GameData/`) or the game folder: `base`'s paks, saved games (`saves/*.sav`) and recordings (`demos/*.dm_15`), and `jk2gamex86.dll` beside it |
| File system | Elite Force's: `base`'s loose files over its paks |
| Shader scripts | Read from `shaders/` (1187 in 1.02a); its 18 directives of its own all read: `oneshotAnimMap`, `clampAnimMap` and surface sprites drawn (the grass and ferns on Yavin's levels), `videoMap` waiting for RoQ, the rest with nothing to draw ([shader.md](../../formats/shader.md)) |
| Maps | All 41 read (Raven's RBSP: light styles, shared light grid points, the level's fog) and drawn in `reluva-viewer`'s World and Maps pages, every light style full until the game's code sets them ([rbsp.md](../../formats/rbsp.md)) |
| Models | Its MD3 models read (276) and placed by the maps' entities; Ghoul2 not yet |
| Game code, menus, sound, cinematics | Not yet |

What's next, and the order of the work: [PLAN.md](PLAN.md).

## Using it

You need the game: the English Windows CD (or a disc image), or an install
made by its own setup. *Add Game...* in the launcher takes the folder
holding `base` (an install's `GameData`); *Import Game...* takes the CD,
the install's folder or `GameData`, and copies what Reluva needs into its
library.

It can't be played yet: `reluva` says so. `reluva-viewer` shows its maps
(World, Maps), its MD3 models, textures and files; Ghoul2 models come in
phase 3 of the [plan](PLAN.md).

## This folder

| Page | What's in it |
|---|---|
| This page | The releases, where it stands |
| [PLAN.md](PLAN.md) | What the game holds, the phases, the decisions |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **Ghoul2:** Raven's skeletal models: meshes (`.glm`) skinned to a
  separate skeleton with its animations (`.gla`).
- **ICARUS:** Raven's scripting system for single player's levels and
  cutscenes; its compiled scripts are `.ibi` files, run by the game's DLL.
- **RBSP:** Raven's version of Quake III's BSP map format, with light
  styles.
- **String package:** a StripEd file (`strip/*.sp`) holding the game's
  text by reference name, in each language.
