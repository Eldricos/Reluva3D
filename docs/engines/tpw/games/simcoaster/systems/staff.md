# SimCoaster: staff

> **Status:** partial · **Code:** `src/engines/tpw/park/staff.cpp`, `src/engines/tpw/ui/hire_window.cpp` · **Checked against:** the recording, the general settings · **Confidence:** guessed unless marked

## Overview

A park's staff are hired from agencies: engineers, janitors, gardeners,
guards, entertainers and scientists. The Staff menu's Hire Staff
(`UITEXT` 827) opens the Hire Staff window, which lists each kind's
candidates; hired, they walk the park's paths and do their kind's work,
paid each month. In the recording the player hires gardeners and
engineers at 11:06. The general settings (`levels/Standard.sam`) give
the numbers, many with their makers' comments.

## How it works

### The candidates

Each kind has its pool of candidates (`StaffPoolInfo`): at the start
`BeginningNumberOfHandymen` and the like (5, the scientists 4), topped
up to that every `TimeBetweenStaffUpdates` (90) days, never past the
kind's `Max` (6). Each has:

- a name, from their kind's names table (`HANDYMAN_NAMES.str`,
  `MECHANIC_NAMES.str`, `GARDENER_NAMES.str`, `GUARD_NAMES.str`,
  `ENTERTAINER_NAMES.str`, `RESEARCHER_NAMES.str`, 35 each), not another's
  of the kind;
- an agency (`StaffAgencies`, their names `UITEXT` 110 to 113: Cut Price
  Recruitment, Staff U Like, Reasonable Recruitment, Star Staff), of
  those the park's promotion level allows: the first two, as the
  recording's window offers *(inferred)*;
- experience, the kind's average (`AvgExperienceOfHandymen`, 20,000),
  give or take `AvgWageDifference` (15,000, "start exp. range can be
  from 5000 - 35000", its comment says), not below `MinExperience`,
  raised by the agency's `InitialExperienceModifier` percent; at most
  100,000;
- a monthly wage, the agency's `InitialWageModifier` (10, 20, 30, 50)
  raised by half again at the most experience. The recording's
  gardeners fit it: Staff U Like's at § 24, their experience bars about
  two-fifths full; Cut Price's at § 11, about a fifth *(guessed)*.

### The Hire Staff window

At 640 × 480, as the recording shows it at 11:06, built of the Buy
window's pieces ([building.md](building.md#the-buy-window)):

| Piece | Model | Where |
|---|---|---|
| The window | `!window_s` | (70, 62), 555 × 350; its title, "Hire Engineers", "Hire Janitors", "Hire Gardeners", "Hire Guards", "Hire Entertainers", "Hire Scientists" (214, 212, 213, 216, 215, 217), at (100, 69) |
| The tabs | `tab_sengnr`, `tab_scleaner`, `tab_sgardenr`, `tab_sguard`, `tab_sentertnr`, `tab_sscientst` | from (108, 143), 42 × 48 each |
| The kind | `!frame` | (508, 98), 90 × 80: its sprite standing, turning round |
| The panel | `!f_plain` | (83, 188), 528 × 172 |
| The columns | `!cbut` | at y 205, 32 high: "Candidate Name" (105, 110 wide), "Experience Level" (219, 112), "Monthly Wage" (334, 112), "Supplying Agency" (450, 112) (218 to 221) |
| The list | `!frame` | (99, 241), 468 × 105; a row 13 pixels from y 243: the name at x 105, experience as a bar from x 220, 107 long, the wage right at x 399, the agency at x 450 |
| Hire, cancel | `b_okay`, `mb_cancel` | (544, 368); (582, 371) |

Its tick hires the chosen candidate, and it stays open for more. With
no candidates it says so (225); scientists want a research room
(`UsageInfo.ThinkingSpace`) first (222).

### Their work

Hired, they come in where the visitors' way in ends and walk the paths
as visitors do, faster with experience, up to `MaxSpeedDifference`
(150) percent. Each kind's settings give its work's range and how long
it takes, from the least experienced to the most, which Reluva scales
between by experience:

- **Janitors** (`Handyman.`): spot litter within `MinDetectionRange` to
  `MaxDetectionRange` cells (3 to 5), walk to it by the path, and sweep
  up what's on its cell over `MinWorkDuration` to `MaxWorkDuration` (40
  to 5, taken as tenths of a second *(guessed)*), stooping with pan and
  brush, their sprite's fifth animation.
- **Guards**: no prank is played within their `MinDetectionRange` to
  `MaxDetectionRange` (5 to 10) cells.
- **Entertainers**: those within their `MinActiveRange` to
  `MaxActiveRange` (2 to 5) cells are cheered by a quarter of their
  grade's `EntertainerConstsPerGrade[n].HappinessEffectOnCell` (5 to 20)
  a day, their grade their experience by 20,000s *(guessed)*.
- **Engineers, gardeners**: walk the paths; there are no breakdowns or
  plants to tend yet.

Their sprites are `Handymen/Spr_ha`, `Mechanics/Spr_om`,
`Gardeners/Spr_gd`, `Guards/Spr_gu`, `Researchers/Spr_re`, and the
world's entertainers' (`Science/Entertainers/SPR_BU`). On each month's
first day the park pays their wages.

## In Reluva

`Park` (`src/engines/tpw/park/staff.cpp`) keeps the pools, hires
(`Park::hire`), pays and works the staff; `HireWindow`
(`src/engines/tpw/ui/hire_window.cpp`) is the window, which `playPark`
opens from the Staff menu; `ParkScreen` draws the staff.
`reluva-tool tpw hire-shot <game folder> <world folder> <out.png> [tab
[row]]` draws the window and lists every kind's candidates; `park-shot
... -hire janitor 2` hires before running the park. The unit tests
(`tpw.unit.park`) set the pools from settings built in the test, hire,
pay, and have a janitor sweep up a kiosk's litter.

## Sources and checks

- **Where the facts come from:** the general settings and their
  comments; the string tables; the interface's models; the recording's
  window at 11:06.
- **How it was checked:** `hire-shot` beside the recording at 11:06: its
  pieces within a few pixels, the wages the same two sums; `park-shot`
  with two janitors over 25 minutes: 3 litter left against 21 without.

## Open questions

- How the game makes its candidates, their experience and wages, and
  when the pools change; the promotion levels.
- Staff's energy, happiness, motivation, wage demands and strikes, the
  staff room, training, patrol areas (`UITEXT` 108, 109), and the other
  Staff menu items.
- What `MinWorkDuration` is measured in; engineers' and gardeners' work.
