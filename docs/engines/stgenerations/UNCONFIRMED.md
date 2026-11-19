# Star Trek: Generations: unconfirmed items

What the engine does that hasn't been compared with the original yet:
worked out from `sgens.exe` and the game's data, but not seen running in
the original. Each could be checked by watching the original where it
happens; [TESTING.md](TESTING.md#still-to-check) has what to look at.

## Read from the code, not seen running

### Seen in the scripts, not yet in the original

- **Some map marker styles** (the automap's markers the scripts set with
  SET_ACTOR_MAP_MARKER, beside those seen): drawn as the code reads them.
- **Space combat's enemy view** (V): the view from the target back at the
  Enterprise, as the code lays it out.
- **The transitions' styles** (SET_TRANSITION_STYLE, `game/mission.cpp`):
  the value goes to the blends with their colour, as in the original; what
  each style looks like there hasn't been seen.

### Space combat

- **Later waves fading in (SC9, the only fight with them):** the original
  makes later-wave ships fully cloaked and uncloaks them, with a sound,
  when their wave comes in (`0x4A2FA9`), but cloaking and uncloaking act
  only on ships with flag +0x2C 0x2000, which nothing in the code was seen
  setting. Here they appear at once (`game/combat.cpp`).
- **Where a shield hit shows:** as read, the flash sits twice the ship's
  largest extent out from its middle, towards the shot, a small dome for
  any ship but the Enterprise (`0x497D2E`). That seems far out; it's built
  as read (`game/combat.cpp`, `drawView`).

## Seen running, not traced in the code

- **The opening radar:** in a playthrough video, Halee's fight (SC16) opens
  with only the target on the radar, though the mission file has all four
  Birds of Prey within range. Here all four show; nothing in the code read
  so far explains it (the radar is `0x4AB102`; no ship cloaks at the start,
  and the fleet's `Cloaked=TRUE` in UNIVERSE.INI was only traced to the
  star map).

## Inferred

### Inferred from the code (small gaps)

- **What "dim" means on the schematics:** each system's part of a ship's
  schematic is bright when working, dim when damaged and red when
  destroyed; the original's flags behind bright and dim (`0x49EAF6`)
  weren't fully decoded, and this is the reading that fits the pictures
  (`game/combat.cpp`).
- **Fleets marked `Cloaked`** (UNIVERSE.INI) stay off the star map and
  start no fight until the story brings them in; whether the original also
  carries it into the fight isn't known (`game/bridge.cpp`).
- **The scripts' second word array** (F2/F3) holds 4096 words; the
  original's bound isn't known (`game/mission.cpp`).
- **The player's health** starts at 50 of 50: the original takes it from a
  character record not yet decoded; its health bar holds about 54
  (`game/mission.h`).
- **A sky's height**, when a level's header doesn't give one that fits, is
  worked out from its pixels (`formats/level.cpp`, `guessSkyHeight`).
- **Three actor animations** (ACTOR_STATE_1, 4 and 5) are taken to be pain
  and deaths, from where the scripts use them (`formats/opcodes.cpp`).

## Guessed

### Assumed

- **The screen:** 640x480 in 256 colours (`game/screen.h`), with the
  bridge, space combat and the movies in true colour.
- **The fonts' colours** refer to the shared palette (`game/generations.cpp`).
- **How see-through the shields are:** the shield materials carry a
  parameter (4096 for the hit dome, 11000 for the facing spheres) whose
  meaning wasn't traced into the DRE library's drawing; here both are
  mixed half and half with what's behind, unlit (`game/combatrender.cpp`).
- **Frame rates:** where the original steps something once a frame (space
  combat's streaming stars), it is taken to run at twenty frames a second
  (`game/combat.cpp`).

## Settled

- **SET_PLAYER_CONTROLS' bits** (`0x43AD04`): 1 walk and strafe, 2 turn, 4
  look up and down, 8 jump, 0x10 crouch, 0x20 the view bobs, 0x40 in low
  gravity walking goes where the view points (`game/mission.h`). Settled
  from the code.
- **Steering by holding the mouse on the viewscreen** (`0x4AEFD4`,
  `0x4A6F7D`): left of the middle turns left, above it climbs. Settled
  from the code.
- **Which way Mouse Look turns** (`0x43C7D4`): towards the mouse, as the
  Left and Look Up keys do. Settled from the code.
- **View overlay mode 2** (`0x409DDC`): it differs from mode 1 only for a
  picture wider than the view, which Epsion's SHELL.OVR (640 wide) never is
  here. Settled from the code.
