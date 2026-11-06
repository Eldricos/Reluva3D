# Classic variants and round duration

The native U.S. English Classic slice offers Original and Master through the
launcher's game options and the in-game [Classic setup](frontend.md). Original uses 4x4 dice and a three-letter minimum;
Master uses 5x5 dice and a four-letter minimum. These are variants of Classic,
not implementations of Boggle's other game modes.

Select **Classic variant** and **Round duration** before starting the game.
Both variants default to three minutes. Timed durations are 1:00 through 5:00
in 30-second steps. **Not Timed** disables expiry; use End Round / F1 to finish
and review pending dictionary words. The selected duration appears before the
round starts and applies again on replay. New Board changes the board while
keeping the selected variant and duration. Changing these options in the
launcher affects the next launch, not a running round.

The Game selection menu option (`classic_menu`, default `1`) enables native
selection/setup before play. Disable it with `0` for direct Classic entry.
Menu changes to variant/duration apply for the engine session only.
**Intro and menu animations** (`frontend_movies`, default `1`) independently
controls the Cinepak frontend clips and intro soundtrack; `0` skips them.
It does not change Classic roll/lid animations. See [movies](movies.md).

The launcher persists its per-target settings as `classic_variant` = `original`
or `master` and `classic_seconds` = `0`, or 60..300 in steps of 30.
An absent setting uses Original / 180 seconds. Invalid saved values produce an explicit
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
  multiplying by 30. This establishes an internal numeric conversion, not
  which positions a player can select in the complete UI. The language DLL's
  dialog resources label whole minutes and include Not Timed. The owner's
  corrected VM check confirms selectable durations from 1 to 5 minutes in 30-second
  increments, plus no timer, matching the internal conversion. Native choices
  and validation expose these values.
- Classic's scorer `0x41a040` uses word length and the variant's minimum.
  The previously documented 66 scorer and 881 adjacency comparisons include
  a four-letter minimum and all 5x5 cell pairs. Master scores four letters as
  one, five as two, six as three, seven as five, and eight or more as eleven.
  Complete Qu faces contribute two letters while using a single cube.

An external reference harness executed the owner's options loader and slider
handlers with bounded profile/window callbacks. All 28 default/timer/position
checks passed: default 180 seconds, the internal timer conversions, Not Timed,
and all nine slider positions. It also checked the slider's range. Original
bytes, resource text, disassembly and reference harnesses remain outside Git. The native
implementation independently parses settings, chooses data readers, applies
board-based rules and uses its own clock model; it executes no original code.

The owner subsequently confirmed Master's three-minute default, four-letter
minimum, 30-second duration increments, no-timer option and the same scoring
table as Original in the original-game VM. These observations corroborate the
Master rules and, following the owner's correction, the full set of UI duration
choices established by the slider checks. Native Windows checks of this new
Master/options build remain separate from the owner's earlier successful Original workflow tests.
Matching AI, networking, background play, cube orientation, original menus,
full original animation overlays are outside the variant change.
[WAV background music](music.md) is available through a separate selector. Classic lid clips and sound effects are now supported by the
[presentation layer](presentation.md), with independent on/off launcher settings.

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

## Installed artwork

**Classic installed artwork** (`classic_artwork`, default `1`) enables the
installed scenes, player/word panels and animated hourglass when available.
Older copies without the additional bundle use the prototype view. Set `0`
to select that view explicitly. This does not mute audio or skip round clips.
See [layout and resource requirements](layout.md).

## Cube orientation

**Auto-orient cubes** (`classic_auto_orient`, default `1`) keeps all English
letters upright in both Original and Master. Disable it before launch to give
individual cubes a 0, 90, 180 or 270 degree display rotation. This also applies
to the fallback view and rotates Qu as a complete face.

The owner observed an auto-orient option in the original VM: enabled cubes
share an orientation; disabled cubes can have quarter-turn orientations.
The native implementation uses an independent deterministic presentation seed.
It does not consume the board generator or alter letters, adjacency, submitted
words, scoring or hit-test geometry. Replay keeps orientations; New Board uses
the new board seed. The native default preserves the existing upright view;
the original default and exact orientation generator are not established.
The owner subsequently confirmed that cubes start randomly oriented when
auto-orient is off and cannot be rotated manually. Native behavior follows
that observation.

## Word-list font

**Classic handwriting font** (`classic_handwriting`, default `1`) uses the
installed KPSI font in the artwork word list. Set `0` for the built-in font;
older imports without KPSI also retain it. See [font support and limits](fonts.md).

Normal launches and New Board use fresh native board seeds. Developers can
retain controlled runs through the strict `classic_seed` target setting; see
[seed behavior and reproducible checks](randomization.md). This adds no launcher
option and does not affect the installed dice resources.

On Players, C / the mode button now selects Traditional or [Chess Timer](chess.md)
for local humans and [computer opponents](computers.md). This is separate from the original background-computer/cursor
options. The private `classic_turns` target value also supports controlled runs;
no new launcher checkbox is added.

On Players, B / Computer Background Play independently switches hidden computer
participation on or off when both a human and computer are assigned. The
session setting `classic_background` accepts only `0` and `1`, defaulting to `0`.
All-computer rosters use visible turns. This adds no launcher checkbox or import
file; see [background play and native timing limits](background.md).
