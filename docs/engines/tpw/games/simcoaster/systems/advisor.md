# SimCoaster: the advisor in the park

> **Status:** partial · **Code:** `src/engines/tpw/ui/advisor.cpp` · **Checked against:** the recording's picture and sound · **Confidence:** inferred unless marked

## Overview

In the park the advisor is a blue creature with white gloves and a bow
on its antennae, who pops up at the bottom right of the screen, over the
"?" button, and talks: a line of the speech bank as the player first
does something. The recording has him from 2:44 on, dozens of times. No
text comes with what he says: the game's string tables hold none of it.

## How it works

### Him

Each world has its own advisor model (`levels/science/advisor/Advisor_SCI.MD2`,
`Advisor_ARA`, `Advisor_WAT`) and 26 animations (`Advisor_SCIm1.MD2`
on). His textures are the front end's advisor's
(`global/Advisor/textures/`). The model holds all he ever shows at once:
his body (`Body`, `Bug Head`, his antennae, eyes, hands), and his props
and costumes beside it: hats for each job (`Security Hat`, `Handyman
Hat`, `Research Hat`, `Garden hat`, `Bowler Hat`, a hard hat and
others), a mouth for each sound (`Mouth - Aah`, `Eee`, `Ooh`, `Sss`, for
the lip-sync of `global/Speech/lips.wad`), shut eyes, a golden ticket, a
letter, award ribbons. No animation hides any: the game chooses what
shows. The recording shows him bare most often, in a mortarboard, a
police cap or an army cap at times.

He stands along his model's z, as the gates do ([fly-in.md](fly-in.md#the-fixed-items)),
facing −y.

### What he says

The speech bank (`global/Speech/speechHD.SDT`, 1,136 lines,
[sound-maps.md](../formats/sound-maps.md)) has no names but numbers.
The recording's sound, matched against every line by its spectrum (each
line's 24 bands' energies slid along the recording's), finds the front
end's three lines where they're known, and in the park 40 more, 0.73 to
0.96 alike. Where the player first does something:

| Line | At | What the player does |
|---|---|---|
| 1000 | 1:51.9 | The briefing of Operation Expand! shows |
| 1120 | 2:06.1 | Its details page |
| 959 | 2:23.7 | The Challenges tab |
| 380 | 2:44.8 | The Buy window opens |
| 502 | 2:55.4, 3:22.8 | A shop bought, being placed |
| 376 | 4:08.0 | The path tool |
| 965 | 5:40.2 | Visitor Statistics opens |
| 1112 | 6:06.5 | A ready-made coaster chosen |
| 1109 | 11:52.4 | The gates' obstacle clears: Operation Expand! done |
| 864 | 13:25.8 | The Research Lab opens |

The others come without a clear cause on the screen: tips, it seems,
or what the park's doing.

## In Reluva

`Advisor` (`src/engines/tpw/ui/advisor.cpp`) shows him at the bottom
right while he talks, his first animation playing, his body only, and
says a line once a park for each of the Buy window, the Challenges tab,
Visitor Statistics, the Research Lab, the path tool, placing an
attraction and the mission done, one after another; aloud with the
game's sound, else timed as 6 seconds. `reluva-tool tpw window-shot
<game folder> <world folder> <out.png> advisor` draws him. The unit
tests (`tpw.unit.ui`) cue him.

## Sources and checks

- **Where the facts come from:** the advisor's models; the speech bank;
  the recording's frames and sound.
- **How it was checked:** the matching beside the front end's known
  lines (1018, 1019, 1020 at their times); `window-shot advisor` beside
  the recording at 3:02, his head where the recording's is.

## Open questions

- What sets off each of the other lines; whether a line comes again
  (502 did, 27 seconds on); the briefing's lines, which Reluva doesn't
  say.
- Which animation he plays, which hat he wears, his lip-sync
  (`lips.wad`) and his eyes' blinking.
- Whether he can be turned off ("You can turn the advisor on and off
  from the Options screen", `TOTD` 20).
