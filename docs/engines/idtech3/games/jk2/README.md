# Star Wars Jedi Knight II: Jedi Outcast

Game id `jk2`, on the [id Tech 3 engine](../../README.md); its own code
will be in `src/engines/idtech3/jk2/`. Raven Software's, 2002: Quake III's
engine as Raven changed it, a generation after Elite Force's.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English, Windows, 1.02a | `base/assets0.pk3`, MD5 `bbd8243669d551c910cfe60ccb4e5edd`, 534,984,353 bytes | Not yet recognised: see [PLAN.md](PLAN.md) |

The same release's other files, for detection and the import:

| File | MD5 | Bytes |
|---|---|---|
| `base/assets1.pk3` | `e91efd51a8883b1c2590e8be7705877f` | 86,950,705 |
| `jk2sp.exe` (single player; says `JK2: v1.02a`) | `494697528df318aac065f5b024a4ef47` | 999,482 |
| `jk2mp.exe` (multiplayer; says `JK2MP: v1.02a`) | `3dbfb179aa803eb7f7a0912556361723` | 1,126,400 |
| `jk2gamex86.dll` (single player's game code) | `acc8fc1cea330d1a0ac0646d3bd96fad` | 1,318,912 |

`version.inf` beside them says version 1.02a for both. On the CD the game
is in `GAMEDATA/GameData/` as plain files, the same files an install has;
the cabinets beside it hold only the installer's own files.

## Where it stands

Nothing runs yet. What the game's files hold, what Reluva already has of
it, and the order of the work: [PLAN.md](PLAN.md).

## Using it

Not playable yet.

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
