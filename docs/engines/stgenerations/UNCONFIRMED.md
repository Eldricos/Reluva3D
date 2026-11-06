# Unconfirmed details

What the engine does that hasn't been compared with the original yet:
worked out from `sgens.exe` and the game's data, but not seen running in
the original. Each could be checked by watching the original where it
happens; [TESTING.md](TESTING.md#still-to-check) has what to look at.

## Seen in the scripts, not yet in the original

- **Some map marker styles** (the automap's markers the scripts set with
  SET_ACTOR_MAP_MARKER, beside those seen): drawn as the code reads them.
- **Space combat's enemy view** (V): the view from the target back at the
  Enterprise, as the code lays it out.
- **The transitions' styles** (SET_TRANSITION_STYLE, `game/mission.cpp`):
  the value goes to the blends with their colour, as in the original; what
  each style looks like there hasn't been seen.

## Inferred from the code (small gaps)

- **SET_PLAYER_CONTROLS** keeps its value (0x5F by default), but what each
  bit allows isn't known (`game/mission.cpp`).
- **The scripts' second word array** (F2/F3) holds 4096 words; the
  original's bound isn't known (`game/mission.cpp`).
- **The player's health** starts at 50 of 50: the original takes it from a
  character record not yet decoded; its health bar holds about 54
  (`game/mission.h`).
- **A sky's height**, when a level's header doesn't give one that fits, is
  worked out from its pixels (`formats/level.cpp`, `guessSkyHeight`).
- **Three actor animations** (ACTOR_STATE_1, 4 and 5) are taken to be pain
  and deaths, from where the scripts use them (`formats/opcodes.cpp`).

## Assumed

- **The screen:** 640x480 in 256 colours (`game/screen.h`), with the
  bridge, space combat and the movies in true colour.
- **The fonts' colours** refer to the shared palette (`game/generations.cpp`).
