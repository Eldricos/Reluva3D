# SimCoaster: the mission's briefing

> **Status:** partial · **Code:** `src/engines/tpw/ui/briefing.cpp` · **Checked against:** a recording · **Confidence:** inferred unless marked

## Overview

As a park opens, after the camera has flown in along the road, the game
shows the mission's briefing over it: the "Current Objective" window,
with the president's letter setting the objective, and, turned over,
the objective's requirement and how far the park is with it ("Go to
Details", "Go to Briefing"). "Proceed" or the red cross closes it, and
the park's clock, stopped meanwhile, runs again. From 1:50 in the
recording, its first mission, Operation Expand! ([park.md](park.md)).

## How it works

### What it says

All of it is the game's own text, by the mission's number (0 for
Operation Expand!):

| What | Where it comes from |
|---|---|
| "Current Objective", "Objectives", "Challenges", "Proceed", "From:", "To:", "Subject:", "Go to Details", "Go to Briefing", "REQUIREMENTS FOR THIS OBJECTIVE:", "OBJECTIVE STATUS:" | `UITEXT` 461, 463, 464, 82, 856, 855, 857, 988, 989, 83, 86 |
| The mission's name, as the letter's subject | `OPERATION`, by the mission's number |
| The letter, its target in its `{0}` | `MISSION_DESC`, by the mission's number; the target is the mission's `ExitStatistic[0].TargetValue` |
| From, and the signature: "Mr Maybury", "President, SimCoaster" | `TAG_SYSTEM` 173, 174 |
| To | The player's name, as the Personnel File has it ([personnel-file.md](personnel-file.md)) |
| The requirement and its status: "Get 40 people in the park at once.", "Number of people currently in the park: 0" | `STAT_TITLES`, a pair for each kind of objective: 11 and 12 for the mission's statistic 8, the visitors in the park; 7 and 8 for statistic 2, a profit, as the missions' targets say (40 and 200 people, 50,000 of profit) *(inferred)*. The others' pairs aren't matched |

The letter is broken into lines 258 pixels wide in `GAME8AA`, as the
recording's are: its first line ends "but we won't buy you", the next
"how to run a", line for line the same. The signature follows a blank
line.

### Its pieces

At 640 × 480, as the recording shows them at 1:52 and 1:58, each a model
of `ui.wad` ([ui-models.md](../formats/ui-models.md)):

| Piece | Model | Where |
|---|---|---|
| The window | `!window_s` | (60, 8), 570 × 394; its title in `TITLEMED` |
| Help and close | `b_help`, `b_close` | (557, 14) and (585, 14) |
| The tabs | `tab_text`: the shown one's sixth mesh, lighter | (95, 51) and (190, 51), 92 × 28 |
| The panel within | `!f_plain` | (80, 78), 538 × 312 |
| The mission's name | in `TITLEMED`, centred | over (82, 104), 226 across |
| The picture's plate, and the picture | `!frame`; `objectim` | (102, 147), 188 × 146; the picture 128 × 128 at (132, 156) |
| The letter's plate | `!frame` | (310, 87), 296 × 262; its text from (318, 96), 11 pixels a line; the labels yellow |
| The president's portrait | `mugshot`, the first (the front end's advisor, [front-end.md](front-end.md#the-portrait-and-the-button)) | (556, 92), 44 × 46 |
| The bar | `b_updown`, where the model puts it: green arrows down (yellow under the pointer) to the details, up back | (318, 313), 284 × 32 |
| Proceed | `!cbut` in greys | (392, 360), 135 × 23 |
| The red cross | `b_cancel` | (577, 356) |

`objectim` has 17 pictures: the three worlds' gates (Arabian Nights',
Land of Invention's, Polar Zone's), a scroll, money, a bridge, and four
each of the worlds' obstacles (`arab1` to `arab4`, `science1` to
`science4`, `water1` to `water4`). Operation Expand!'s is `science1`,
the fence: the tenth. Which picture the other missions show isn't known.

## In Reluva

`Briefing` (`src/engines/tpw/ui/briefing.cpp`) draws the window as
above; `showBriefing` shows it over the new park until it's closed (by
Proceed, either cross, Esc or Enter), the park's clock stopped
meanwhile. The Challenges tab is drawn but not yet shown.
`reluva-tool tpw briefing-shot <game folder> <world folder> <out.png>
[player [-details]]` draws it over a new park and prints its text;
`park-run` turns it to the details and back and proceeds
([park.md](park.md#in-reluva)).

## Sources and checks

- **Where the facts come from:** the recording's briefing, 1:50 to
  1:58; the string tables, the missions' settings and the interface's
  models.
- **How it was checked:** `briefing-shot` set beside the recording at
  1:52: the letter's lines break where the recording's do; the pieces
  within a few pixels of its.

## Open questions

- The other missions' pictures; the objectives' other kinds' strings.
- The Challenges tab: the park's challenges (`Challenges.sam`).
- When the game shows the briefing again, and what the window's help
  button opens.
