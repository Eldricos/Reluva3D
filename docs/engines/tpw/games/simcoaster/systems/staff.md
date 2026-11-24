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
- a monthly wage, short of what their experience expects (their kind's
  `MaxWage`, "Handymen with maximum experience would only expect to earn
  120", the comment says, at the most experience), by the agency's
  `InitialWageModifier` plus 50, percent: Cut Price Recruitment's 60,
  Staff U Like's 70. It fits all the recording's: the gardeners' § 11
  and § 24, their experience bars about a fifth and two-fifths full; the
  guards' § 48 and § 72, theirs longer; an entertainer's § 12, theirs
  short *(guessed)*.

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
- **Engineers** are sent to a ride broken down, or worn below
  `RideBreakDown.Imminent` (10 percent, "call engineer automatically
  when repairstate falls below this", its comment says), the nearest
  free one, or when the player calls one from its menu; they mend it, by
  the path beside its entrance, to whole ([building.md](building.md#wear-and-breakdowns)).
- **Gardeners** water the plants (`UsageInfo.RequiresWatering`: flower
  beds, bushes, trees) gone below half within their range, or one the
  player calls them to. A plant dries from full over 60 days *(guessed)*;
  dry, it adds nothing to the park's points. Their models show nothing
  of it: their five main animations are the same plant swaying.

Each job takes some of their energy (*guessed*).

### Their lives

Each has energy, happiness and motivation, 0 to 100, starting full,
and at 80 and 80 *(guessed)*:

- **Energy** goes a quarter a day at work *(guessed)*. Below
  `AllStaffConstants.StaffSlackingEnergy` (50), "and there is a staff
  room closer than" `MinDistanceForSlacking` (10) cells, "then member of
  staff will go and rest for a bit", the settings say: in the staff room
  (number 1400: it has no setting of its own that says so) they recover
  by their `Min..MaxRecuperationRate` (0.2 to 0.75, tenths of a day's
  rest a day, *guessed*) and their happiness by
  `Min..MaxHappinessRecuperationRate` (1 to 3) a day, by experience and
  their agency's `RecuperationModifier` and
  `HappinessRecuperationModifier` (tenths), till full. Tired and with no
  room to rest in, they lose a point of happiness a day.
- **Wages**: on each month's first day, what they're paid is set
  against what their experience expects: their motivation and happiness
  rise by `HappyWageVal` (2) if it's enough, fall by `UnHappyWageVal`
  (5) if not. Below `LowestWageHappiness` (20) they demand a raise of the
  difference (`UITEXT` 253, "I want a raise of ${13} or I'm leaving").
  The player offers some: the whole, they stay content (255); within
  `PercentageForBadWageDecision` (10 percent, "they'll just about live
  with it"), they stay, for now (256); less, they insist (257), and the
  player accepts the demand (258) or lets them go (259). A raise given
  otherwise lifts their motivation by its share of what they had
  *(guessed)*.
- **Strikes**: after `Strike.MinMonthsForStrike` (12) months, a kind of
  at least `MinStaffOfTypeForStrike` (3) whose average energy, happiness
  or motivation is below `EnergyPercentToStrike` (20),
  `HappyPercentToStrike` (30) or `MotivationPercentToStrike` (30) goes on
  strike, checked monthly: they stop work, stand at the world's strike
  area (`FixedItemInfo.StrikeAreaStart`, `StrikeAreaSize`) with their
  signs, and none of the kind can be hired (224). The strike ends, checked
  daily, when their averages are back above.
- **Training**: sent to the training room (`UsageInfo.TrainingRoom`),
  for 30 days *(guessed)*, they come out `TrainingExtraExperience` (5,000)
  more experienced, by their agency's `TrainingExperienceGainModifier`
  (tenths). They need `MinExperienceForTraining` (30,000) first (232), and
  the park a training room (233).
- **Patrol areas**: kept to a rectangle of cells, two corners clicked,
  they wander and look for work only within it.
- **Firing**: gone at once.

Their sprites are `Handymen/Spr_ha`, `Mechanics/Spr_om`,
`Gardeners/Spr_gd`, `Guards/Spr_gu`, `Researchers/Spr_re`, and the
world's entertainers' (`Science/Entertainers/SPR_BU`); on strike, those
with one (the handyman's sixth animation) hold up a sign. On each
month's first day the park pays their wages.

### The windows

The recording opens no staff window but the hiring one; these are laid
out in its and the attraction window's style, with the interface's
pieces (`b_minus`, `b_plus`, `b_train`, `b_fire`, `b_arleft`,
`b_arright`), their strings `UITEXT`'s *(guessed)*:

- **A member of staff's**, clicking them: their sprite turning; employed
  since (190), monthly wage (191) with − and +, current status (192, the
  staff's states of `THOUGHTS.str`: happy, tired, going on strike, to the
  training room, to the staff room, to a job), experience, energy,
  happiness and motivation as bars (193 to 196), their agency (197); train
  and fire.
- **Their menu**, right-clicking them: set or clear their patrol area
  (603, 605), send for training (606), fire (608).
- **List All Staff** (829): a tab a kind; name, monthly wage, skill,
  happiness (234 to 237), the average happiness (238); a row chosen and
  the tick open their window.
- **Train Staff** (828), the Training Room (226): those in training, their
  type, ability to learn and progress (227 to 230), or none (231).
- **Locate Staff** (830): the camera moves to each in turn.
- **Staff Finance Controls** (831, "Financial Controls For Staff", 260):
  a tab a kind, its wage (265 to 270), the adjustment type (261: set to
  an amount, change by so much, by a percentage, 262 to 264) and the
  amount; the tick applies it to the kind.
- **A wage demand** (251, 254): what they want, the offer with − and +,
  and the answer.

## In Reluva

`Park` (`src/engines/tpw/park/staff.cpp`) keeps the pools, hires
(`Park::hire`), pays and works the staff; `HireWindow`
(`src/engines/tpw/ui/hire_window.cpp`) is the hiring window, which
`playPark` opens from the Staff menu, and the others are in
`src/engines/tpw/ui/park_windows.cpp`; `ParkScreen` draws the staff.
`reluva-tool tpw window-shot <game folder> <world folder> <out.png>
<attraction|gates|staff|list|training|finance|demand|menu> [seconds]`
draws a window over a new park with one of each kind of staff.
`reluva-tool tpw hire-shot <game folder> <world folder> <out.png> [tab
[row]]` draws the window and lists every kind's candidates; `park-shot
... -hire janitor 2` hires before running the park. The unit tests
(`tpw.unit.park`) set the pools from settings built in the test, hire,
pay, and have a janitor sweep up a kiosk's litter; break a ride for an
engineer to mend, dry a plant for a gardener to water, train, set
patrols and wages, answer a wage demand, and strike.

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
- How fast staff tire, recover and learn, and how the game measures
  `MinWorkDuration` and the recuperation rates; experience gained at
  work (`HandymanConstsPerGrade[n].ExperiencePerMonth`); staff rooms'
  capacity; what the windows the recording doesn't open look like.
- The guards' chasing (`WorkDuration`), the scientists' research.
