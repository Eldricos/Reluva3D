# SimCoaster: challenges

> **Status:** partial · **Code:** `src/engines/tpw/park/challenges.cpp`, `src/engines/tpw/ui/briefing.cpp` · **Checked against:** the settings, the string tables, the recording · **Confidence:** inferred where marked, guessed elsewhere

## Overview

The company's directors set the player challenges: sell so many hot
dogs, keep so many visitors in the park, keep every ride in repair.
Each world has ten. One is offered by letter, the player takes it up in
the Challenges tab of the objectives' window, prepares for it if it
gives time to, and then has so many days to do it. Won, it pays golden
tickets, and may pay cash, shares, a rise in the share price or a
promotion, which the missions need ([missions.md](missions.md)). Failed,
the share price falls, and too many failed and the player is fired. The
recording shows the Challenges tab at 2:30, every challenge "n/a" in
Operation Expand!, and a director's letter offering the Staff Happiness
Drive at 24:00, in Operation Manager!.

## How it works

### The settings

`Data/Challenges.sam` describes 30 challenges, `Challenges[1]` to
`[30]`, with its makers' notes:

| Setting | What it is |
|---|---|
| `Type` | what's asked (below) |
| `TargetTime`, `TargetVal`, `TargetObj` | how many days, how much, of which attraction |
| `GracePeriodLimit` | days to prepare before it runs |
| `CashPrize`, `GTPrize`, `SharePrize`, `PromoGiven` | what it wins: cash, golden tickets ("This has a minimum value of 1"), shares, a rank |
| `SharePriceEffectWon`, `SharePriceEffectLost` | the share price's rise and fall, percent |
| `MissionNeededToActivate` | "no challenges until first promo mission" (2) |
| `TriggerData`, `DaysDelay` | what sets it off, as its comment says for its type ("nth hot dog shop built", "n visitors in park", "after nth heatwave"), and the days from then to its offer |
| `Sender` | who sets it: "1 = EMAIL_FROM_PRESIDENT, 2 = EMAIL_FROM_FINANCE_DIR, 3 = ...FACILITIES_DIR, 4 = ...PR_DIR, 5 = ...HR_DIR, 6 = ...TECH_DIR" |

A world's own settings list its ten (`levels/science/Standard.sam`'s
`ChallengesInThisLevel[0]` to `[9]`); Land of Invention's are 2, 4, 5, 14,
13, 18, 21, 8, 20 and 22, the recording's list at 2:30 in its order, by
their titles (`CHALLENGE_TITLES.str`, by the challenge's number)
*(confirmed)*. The general settings give `Challenges.DaysBetweenOffers`
(20, "wait between making challenges available"),
`ShortTimeLeftWarningAt` (20 percent of the time left) and
`FiringLineForRank[n].Limit` (5 at each rank, "because it's annoying to
have to save before taking on any challenges").

### What they ask

Each challenge's three strings in `CHALLENGE_SPECIFIC.str`, from three
times its number less one, are its letter, its status while it's
prepared for, and while it runs. They tell what each type asks:

| Type | Asks |
|---|---|
| 2, 3, 6 | sell so many hot dogs, drinks, balloons in the time: the shops by their `UsageInfo.ShopType`, 2, 4 and 6 |
| 17 | so many visitors use an attraction |
| 45 | so much profit from drinks and ices |
| 10 | keep so many percent of the children in costumes |
| 25 | keep every member of staff's happiness above so much |
| 34 | keep last month's profit above so much |
| 35 | keep the sideshows' customers' satisfaction above so much; Reluva keeps no satisfaction, and takes the visitors' happiness *(guessed)* |
| 36, 37 | keep the visitors' average happiness, their number in the park, above so much |
| 38, 39, 40 | no pranks; none by grown-ups (whining, "spreading vicious rumors"); none by children |
| 41 | no strikes |
| 42 | the hot dog stands shut "for a while", hunger not among the three commonest thoughts |
| 43 | no litter |
| 46 | the Safety Review: every ride's state of repair above so much |
| 48, 50 to 58 | a special ride's: its satisfaction, excitement, repair, users, photos, loops ([special-rides.md](special-rides.md#their-challenges)) |

A count (2, 3, 6, 17, 45) is won as soon as it's reached and failed if
time runs out; a kept measure is failed as soon as it slips and won if
it holds to the end *(guessed)*.

### Offered, taken up, run

Checked each day:

1. A challenge of this world, not won, is set off once the player's
   mission is `MissionNeededToActivate` and its trigger is met: the
   hot dog stands built, the visitors in the park, the visitors to date,
   last month's profit, the pranks, the heatwaves, as its type reads
   `TriggerData`; an attraction's, once that attraction's there and
   open, a special ride once it's activated
   ([special-rides.md](special-rides.md)). Triggers Reluva can't tell are
   met at once *(guessed)*.
2. `DaysDelay` days on, and `DaysBetweenOffers` since the last offer, it's
   offered: "New Challenge Available!" (`TAG_SYSTEM` 42) from its sender,
   ending with its title, as the recording's at 24:00 is from Miss Moody,
   Human Resources, sender 5 *(confirmed)*. It arrives in the Message
   Center.
3. The player takes it up in the Challenges tab, one at a time
   *(guessed)*: prepared for its `GracePeriodLimit` days, if any, then
   run for its `TargetTime` days. "Challenge Progress" (44) says it's
   begun; "Time Warning" (50) when a fifth of its time is left.
4. Won ("Challenge Won!", 46): its prizes; a promotion, "Promotion to
   {4}" (169, 170). Failed ("Challenge Failed", 48): the share price
   falls, and it can be offered again. With `FiringLineForRank` failures,
   the player's fired: "You've failed too many challenges" (167), and
   the park ends.

The senders' letters are `TAG_SYSTEM`'s: the president's between the
titles (43, 45, 47, 49, 51), each director's five in a row from 53 (53,
58, 63, 68, 73 for senders 2 to 6); their names 173, 177, 179, 181, 183,
185 (Mr Maybury, Mr Banks, Miss Buckle, Mr Topp, Miss Moody, Professor
Newton). Which director is which sender is by the order of Sender's
key, borne out by Miss Moody's letter.

### The Challenges tab

At 640 × 480, as the recording shows it at 2:30, in the objectives'
window ([briefing.md](briefing.md)), titled "Current Challenges" (`UITEXT`
462): "Title" and "Status" (`CHALLENGE_GENERIC` 20, 21) at (96, 96) and
(226, 96) over the list in a frame at (92, 132), a row 13 pixels, each
challenge's title and status: "n/a", "Available", "Preparing", "Active",
"Complete" (22 to 26). Below, at (88, 314), "FIRING LINE STATUS: You can
only fail {6} more challenges before you get fired." (19, 18). Beside
them, in the letter's frame, "No challenges are currently available."
(`UITEXT` 465), or the one chosen: "CHALLENGE DETAILS:" (7), its letter,
its prizes ("If you succeed you'll receive {0} Golden Tickets.", shares,
"There's a bonus of ${2} too!", the share price's rise and fall, "Winning
will get you promoted to:", 0 to 6), then "CHALLENGE STATUS:" (8) and how
it stands (9 to 16, 27) with its count or measure. A tick (`b_okay`) by
the red cross takes up the one chosen ("Accept selected challenge",
`UIHELPTEXT` 101), where the recording, with none on offer, shows nothing
*(guessed)*. The
Park menu's Challenges (849) opens the window on this tab.

## In Reluva

`Park` keeps this world's challenges (`Park::challenges`,
`acceptChallenge`, `challengeValue`) and runs them daily
(`src/engines/tpw/park/challenges.cpp`); the player's challenges won and
failed are kept in their file ([players.md](players.md)), with their
rank and golden tickets. `Briefing` draws the tab. `reluva-tool tpw
briefing-shot <game folder> <world folder> <out.png> Tester -challenges
[-mission <n>]` runs a park 60 days and draws the tab, the first offered
chosen, and prints the list. The unit tests (`tpw.unit.park`) offer,
fail, win and promote on a world built in the test.

## Sources and checks

- **Where the facts come from:** `Challenges.sam` and its notes; the
  worlds' and the general settings; `CHALLENGE_TITLES.str`,
  `CHALLENGE_GENERIC.str`, `CHALLENGE_SPECIFIC.str`, `TAG_SYSTEM.str`; the
  recording at 2:30 and 24:00.
- **How it was checked:** `briefing-shot -challenges` beside the
  recording at 2:30: the same list, statuses and firing line, the pieces
  within a few pixels; Operation Manager!'s offers read over.

## Open questions

- The coaster's loops and the photos (types 51 and 57), which Reluva
  never offers.
- What each `TriggerData` means where its comment doesn't say; whether
  offers wait while one is under way; whether one may be declined.
- Whether a kept measure fails at once or only at the end
  (`CheckAtEndOnly`); customer satisfaction, which Reluva doesn't keep.
- Whether a failed challenge is offered again, and when.
