# Star Trek: Starfleet Command Volume II: Empires at War

Game id `sfc2`, on the [Starfleet Command engine](../../README.md); its
code is in `src/engines/sfc/`. A real-time starship combat game built on
the Star Fleet Battles board game's rules, with single-player campaigns
on a strategic map (the Dynaverse) and multiplayer.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English retail 1.0 (`StarFleet2.exe` version 1.0.0.2, built 27 November 2000), two CDs, and an install from them | `StarFleet2.exe` and `Assets/Sprites/sprites.q3` (`kVariants`, `src/engines/sfc/sfc.cpp`) | Recognised |
| Patch 2.0.3.6 (October 2002), over the retail install | The same files' names (`kFallback`): "unknown release", as the patch rewrites both | Surveyed; its patched files not yet hashed |
| Other releases | The same files' names: "unknown release" | Not checked |

Detection signatures (MD5 of each file's start, and its size, as
`reluva --hash` prints them):

| File | MD5 (start) | Size |
|---|---|---|
| `StarFleet2.exe` 1.0 | `e78f2d2c9c9c51f2077bb5c10cbaf617` | 4149248 |
| `Assets/Sprites/sprites.q3` 1.0 | `7eaeae9272f5d82ac6523a3d6191a45a` | 78253026 |

## Where it stands

The launcher recognises the 1.0 install, and any other as an unknown
release; starting it shows an empty screen. The plan is in
[PLAN.md](PLAN.md).

## Using it

Not playable yet. *Add Game...* in the launcher, with the install's
folder, finds it.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases, what the install holds, and the decisions taken |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **Dynaverse:** the campaign's strategic map of hexes, held by the
  empires, on which missions are offered ([PLAN.md](PLAN.md#what-the-install-and-the-cds-hold)).
- **BPV:** a ship's Basic Point Value, its cost and strength in Star
  Fleet Battles' rules.
