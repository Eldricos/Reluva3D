# Star Trek: Generations: research notes

Working notes, kept as they were written: they don't follow the page
templates, and they may hold guesses since settled or corrected. What the
engine relies on is on its pages in [formats/](../formats/README.md), each
with its source. How the notes were made is in
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

This folder holds the notes from reading `sgens.exe` (the 2 CD Windows
release: 1571328 bytes, MD5 `5044cd4e8cd2ef067dc79d86b38cc44d`) that the
engine was built from, while it was STG-Engine. Addresses are the
executable's virtual addresses (image base 0x400000). Confidence marks:
**H** read directly from the code (or checked against the game's files),
**M** inferred with a small gap, **L** a guess.
[UNCONFIRMED.md](../UNCONFIRMED.md) collects the M and L points that matter
to the engine.

The notes name STG-Engine's files: its `engines/stg/` is this engine's
`src/engines/stgenerations/` (`formats/` and `game/`;
[PORTING.md](../PORTING.md#where-each-part-went) maps each file). They also
mention the working scripts, disassembly dumps and pictures they were made
with (`re/...`, `S/...`): those weren't kept
([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#working-files-not-included)).

| File | Contents |
|---|---|
| [frontend.md](frontend.md) | Start-up, the title, the captain's menu and its panels, the story's states, `SGENS.CFG`, saved games |
| [bridge.md](bridge.md) | The bridge: the star map, warps, scans, the Nexus, the story's commands, the crew's speech |
| [combat.md](combat.md) | Space combat: COMBAT.DAT's files, ships and their systems, weapons, shields, the enemies' AI, the console |
| [events.md](events.md) | How the main loop sends the mission scripts their events |
| [natives-a.md](natives-a.md) to [natives-f.md](natives-f.md) | The script instructions (natives), a batch a note, with their arguments and effects |
| [panels.md](panels.md) | Script-driven screen features: close-up panels, the automap, screen effects, controls |
| [hud.md](hud.md) | The away missions' screen: its layout and the HUD |
| [actors-ai.md](actors-ai.md) | Actors' AI, movement and animation timing |
| [actors-draw.md](actors-draw.md) | Actors' sprite sets, animation and drawing |
| [billboard-draw.md](billboard-draw.md) | Billboards and translucency in the 3D renderer |
| [movers.md](movers.md) | Movers: doors and lifts |
| [wall-slope.md](wall-slope.md) | What a level wall's +0x20 is (its heading, not a slope) |
| [player-attack.md](player-attack.md) | The player's attack, picking and line of sight |
| [projectiles.md](projectiles.md) | Enemy shots and projectiles |
| [music-speech.md](music-speech.md) | Music, speech subtitles, the engine's sounds and the mixer |
| [sound-table.md](sound-table.md) | The scripts' sound and cue tables, and the sound manager's rules |

## Corrected since

Readings in the notes found wrong later; the formats pages have the right
one:

- [combat.md](combat.md): clicking the viewscreen doesn't enlarge it: held
  there, the mouse steers (`0x4AEFD4`, `0x4A6F7D`; left of the middle turns
  left, above it climbs). See [formats/combat.md](../formats/combat.md).
- [combat.md](combat.md): `0x497B00` isn't a shield facing glowing when it's
  down: it is the Enterprise's jolt (the screen shakes, and dims when a
  facing is badly hurt). See [formats/combat.md](../formats/combat.md).
- [combat.md](combat.md)'s closing plan is the plan for STG-Engine's space
  combat as it stood then; most of it has been done since.
