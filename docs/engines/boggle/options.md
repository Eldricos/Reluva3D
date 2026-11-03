# Classic variants and round duration

The native U.S. English Classic slice offers Original and Master through the
launcher's game options. Original uses 4x4 dice and a three-letter minimum;
Master uses 5x5 dice and a four-letter minimum. These are variants of Classic,
not implementations of Boggle's other game modes.

Select **Classic variant** and **Round duration** before starting the game.
Both variants default to three minutes. Timed durations are 1:00 through 5:00
in 30-second steps. **Not Timed** disables expiry; use End Round / F1 to finish
and review pending dictionary words. The selected duration appears before the
round starts and applies again on replay. New Board changes the board while
keeping the selected variant and duration. Changing these options in the
launcher affects the next launch, not a running round.

The launcher persists its per-target settings as `classic_variant` = `original`
or `master` and `classic_seconds` = `0`, or 60..300 in steps of 30. An absent
setting uses Original / 180 seconds. Invalid saved values produce an explicit
error; the engine does not silently substitute different rules. The original
registry/profile is not read or modified.

## Evidence and scope

Source executable SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Addresses below are executable virtual addresses:

- Selection at `0x40be03` sets Original to 4x4 / minimum three. The branch at
  `0x40be23` sets Master to 5x5 / minimum four.
- Dictionary/dice selection at `0x41df3a` uses the minimum to choose resource
  IDs starting at `0x3714` or `0x371a`. The owner's English language resources
  map these to `Dicts/EngOrg.dst` and `Dicts/EngMas.dst`. Both use the existing
  main English word dictionary. The native renderer loads the corresponding
  observed palette files `ClsO1Pal.pal` and `ClsM1Pal.pal`.
- Classic options loader `0x40d400` reads `NotTimed` (default false). If set,
  its duration is zero; otherwise it reads `GameLength` with default 180
  seconds and converts to milliseconds.
- The Classic options page's slider initializes at `0x424890` with positions
  2..10. Its change handler `0x42bc10` converts position to seconds by
  multiplying by 30. This gives 60..300 seconds in 30-second steps. The
  language DLL's dialog resources label whole minutes and include Not Timed.
- Classic's scorer `0x41a040` uses word length and the variant's minimum.
  The previously documented 66 scorer and 881 adjacency comparisons include
  a four-letter minimum and all 5x5 cell pairs. Master scores four letters as
  one, five as two, six as three, seven as five, and eight or more as eleven.
  Complete Qu faces contribute two letters while using a single cube.

An external reference harness executed the owner's options loader and slider
handlers with bounded profile/window callbacks. All 28 default/timer/position
checks passed: default 180 seconds, each timed choice, Not Timed, and all nine
slider positions. It also checked the slider's range. Original bytes, resource
text, disassembly and reference harnesses remain outside Git. The native
implementation independently parses settings, chooses data readers, applies
board-based rules and uses its own clock model; it executes no original code.

The owner's earlier VM observations and Windows workflow checks concern
Original. A VM corroboration of Master defaults/options has been requested;
this implementation's Master and duration evidence currently comes from the
executable/resources and the scoped reference checks above. Matching AI,
networking, Chess Timer, background play, cube orientation, original menus,
animations and sound is outside this change.

## Native validation

Synthetic tests cover all advertised settings and invalid saved values,
three-letter rejection in Master (including an existing user word), four-
letter acceptance, last-row/last-cube selection, no edge wrapping or cube reuse,
Qu's letter count, user-word approval and replay. Every timed choice has an
exact-deadline test. Not Timed remains active at a large injected clock value
and supports manual finish/replay; a running board cannot be replaced.

Scripted engine records exercise 5x5 drawing and mouse input through cube 25,
minimum-length feedback, a configured one-minute timer before start and at
expiry, and untimed start/finish/replay. Original's prior rendering records
remain unchanged. All fixtures are independently invented.

With the owner's external English data and native seed `0x1997`,
`classic-check <folder> master` accepts 244 dictionary words for 497 points,
with checksum `00300e30cd27a974e8768335fa9502c4`. This is a native reproducibility
check, not a claim that the original shuffle produces the same board. Optional
game-data records contain only counts/checksums. `classic-replay` also accepts
`master` and a duration to compare an externally observed 5x5 board; see the
[comparison procedure](comparison.md).
