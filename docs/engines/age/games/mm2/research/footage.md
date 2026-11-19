# Midtown Madness 2: what the game's own screens show

Some things the game's files don't say (where its HUD goes, how its
damage meter moves) can be seen on its screens. Six public gameplay
videos were watched for this; nothing from them is in Reluva but
measurements and observations, written down here.

| Video | Length | Picture | What's in it |
|---|---|---|---|
| "Midtown Madness 2 Walkthrough" | 5 h 55 min | 640 x 480, 30 a second | Crash Course from the first lesson to the final exam, many Blitz, Checkpoint and Circuit races in both cities, the Vehicle Showcase cars cruising, the in-car views, the options screens |
| "Midtown Madness 2 (2000) - PC Gameplay" | 34 min | 1280 x 720 (the game at that size), 60 a second | Circuit and Checkpoint races in both cities, at night, in rain and fog |
| "Midtown Madness 2 (2000): Old Game PC in 4K 60FPS" | 5 min | 1280 x 720 as fetched | Cruising San Francisco, a few races |
| "Midtown Madness 2 100% Speedrun Gameplay" | 2 h 34 min | 640 x 480 | every lesson and race, the Graphics Options screen |
| "Midtown Madness 2 on Professional 100% in 2hours, 28 minutes" | 2 h 29 min | the game at 960 x 720 in 1280 x 720 | the same at Professional |
| "Midtown Madness 2, 100% - 1:54:43" | 2 h 01 min | the game at 960 x 720, 60 a second | the same |

The walkthrough is the most useful: the game ran at 640 x 480, the size
its HUD pictures were made for, so the HUD's places can be measured in
its pixels.

## How

Frames were taken from the videos with ffmpeg: contact sheets every 10 s
(every minute for the walkthrough) to find what's where, then single
frames at full size where something could be measured. Positions were
read off the frames by colour (the HUD's green, the markers' purple) and
compared with the pictures' sizes in `texture/`. To see how the damage
meter moves, its strip of screen was taken every 5 s through the whole
walkthrough (4,264 samples) and the colour at its ends followed race by
race.

## What they settle

- **The HUD's layout** (Reluva's before was a guess): the speed in big
  `DIGITAC_*` digits bottom left, the gear letter under them, the
  `SPEED_TICKS` bars as the revs, the damage meter's label at the
  bottom; the clock in `DIGI_*` digits top middle as minutes, seconds
  and hundredths; the race's list top left (Place, Check, Lap, the
  laps' times); the map bottom right. Positions are in
  [age-driving.md](age-driving.md) and [age-racing.md](age-racing.md).
  The game's speedometer panel (`SPEED`, `MPH`, `TACOMETER TICKS_HALF`)
  is never on screen in any of the three.
- **The damage meter**: `DAMAGE_LABLE` over a label-wide window of the
  500-pixel `DAMAGE` bar that slides from its green end to its red as
  the car is damaged.
- **The clock's digits** have no alpha in their files, but their black
  is see-through on the screen.
- **The map** turns with the player and keeps the player in its middle;
  it's drawn at about 0.92 of its `.mmHudMap` Size (124 x 110 at
  640 x 480), its top left at Pos.
- **The opponents' markers**: purple triangles over each opponent with
  its place above in `opp_icon`'s digits (stored upside down in the
  file); no digits until the start.
- **The HUD arrow** turns yellow when the checkpoint is behind (its
  model's second paint job), as a loading screen tip says.
- **The race messages**: "Ready...", "Set...", "Go!" and the lessons'
  messages over the road at y 110; after each checkpoint the time so far
  under the car, "Lap time" over it at a lap's end, "Final lap!" there
  too. The words are the game's strings (49-63, 241-261), which also
  name its fonts (Gill Sans MT, by size and weight).

## What they confirm

- Tyre smoke on the roads, the wheels throwing up dust on grass and
  spray in water, as the materials' particles were read
  ([age-driving.md](age-driving.md)).
- The street lamps' glows at night, small and warm.
- In London a traffic light's pole stands on the left curb, where Reluva
  puts it for traffic keeping left.
- The cars' damaged textures (`_dmg`) showing as they're knocked.

## Seen, and since built

- A rear-view mirror (BACKSPACE, per the loading tips) top right,
  x 447-640, y 0-77 at 640 x 480, without a frame: the world drawn
  again, looking back from the driver's head, flipped.
- The loading screens' tips: F1 for the controls, Q to pause on the
  full-screen map, H for the HUD, TAB for the map. They led to the help
  file's keyboard table, now Reluva's default keys
  ([age-menus.md](age-menus.md)). F1's list of controls is built too.
- In Checkpoint and Blitz races, small light blue markers over the
  checkpoints still to reach, which led to the races' checkpoints being
  taken in any order, as the help says, and to the gates themselves:
  the footage shows each as one banner across the road, which is how
  `pt_check` is made ([age-racing.md](age-racing.md)).
- The results screen's message for what was unlocked ("Congrats! New
  paint job for the New Beetle RSi!"), from the cities' `_rewards.csv`.

## What they don't settle

- The music's rules for idling, chases and the pause, the props' Flash
  values and the camera's numbers beyond what's in
  [age-driving.md](age-driving.md): nothing in the videos shows them
  clearly enough to measure. Stops of 5-13 s in mid-race show no clear
  change of music.

## Bridges and music, looked at again

A second pass at the 34-minute video, frame by frame around London's
drawbridges, and through its sound:

- **The drawbridges cycle.** In two runs of London Run, the race's
  `open:` bridge is raised 10-18 s into the race, down about 80 s in
  and raised again about 140 s in. No ships were nearby. Tower Bridge
  was down when crossed in Bridge Bash. Reluva's bridges now cycle every
  130 s ([age-traffic.md](age-traffic.md)).
- **The music's tempo** was measured from the sound: the beat's onsets
  in windows of a few seconds, and the period they repeat at. A race
  starts beatless for about 8-15 s, then the groove comes in, whatever
  the car is doing. A few seconds after "Final lap!" (3-4 s, the next
  measure) London Run's beat goes from 0.50 to 0.45 s, on both runs.
  After Bridge Bash's last checkpoint its 0.43 s groove stops within a
  second and a 0.78 s pattern plays to the finish. Next to the
  segments' own tempos, these fit the race tune's chase segment, and
  Reluva now plays it there ([age-racing.md](age-racing.md)). After the
  finish the beat stops for a few seconds, then the results screen's
  own music plays (0.35 s), then the menu's (0.43 s).
- The opening movie isn't in any of them.

## The three 100% runs

Looked at second by second where it mattered: the races found by their
loading screens, scenes placed on the map by the races' waypoints and
the PSDL, the music's beat measured as before (the 640 x 480 video's
sound runs 0.2% slow against its picture, corrected for), its
speedometer read by digit templates, "Final lap!" found by template.

- **"Final lap!" brings the chase segment**, in 19 of 20 cases in one
  run, 3-6 s later (4.4 on average, the next measure), with each tune's
  own Cops tempo: London 124 to 150 bpm, Bullet 140 to 155, Enemy 120 to
  140, Duck 120 to 134, Pimphand 124.4 to 145. A police chase lesson
  plays the Cops segment too (145 bpm for 107 s, back to 124 about 5 s
  after). Restarting a race doesn't restart the tune. The results list
  fills in a racer a second, the opponents named by grid slot
  ("Opponent 1", strings 13-20), over the Results segment's pattern.
  Reluva does all of these now ([age-racing.md](age-racing.md)).
- **Follow lessons have no clock** and run past their TimeLimit
  (San Francisco's `crash10`, 36, ran 88 s); they end with the car at
  its end. Reluva's no longer timed. What the lesson table's fourth
  extra column does in `crash10` and `crash11` still isn't seen (at
  Professional, where `crash10`'s is 0, the target's marker was on
  screen less of the time, a weak lead).
- **Walls**: the freeway's and the Transbay ramp's `s_wall_veg` walls
  have their rubble stone at the foot and plants along the top, so a
  wall's texture runs up from its first row; Reluva drew them upside
  down until now ([psdl.md](../formats/age/psdl.md)). From below (Gimme
  SOMA, under the Transbay ramp) the decks' edges show a plain light
  band about 2 m deep, the tunnel attribute's flag 0x4; from beside the
  ramp its walls (flag 0x4000) show a plain grey face outside.
- **Railings**: the pier's `s_conc_rail` balustrade and Tower Bridge's
  mesh are see-through; no car touched one. The piers by the sheds have
  none: cars drive off into the bay, and "Sleep with the fishes!" (the
  game's string 30) shows as the car is put back; Reluva shows it now.
- **The Graphics Options screen** of one run: Vehicle Reflections ticked
  and available, Texture Quality High, Object Detail Very High, Cloud
  Shadows High, and no Smart Rendering box (a different version's
  screen from the files Reluva reads). No run uses Low textures, so
  flag 0x8000's reading stays untested.
- **Other things seen**: every race's countdown starts with the camera
  high over the grid, looking down on the car, and brings it down
  behind the car by the start (Reluva does this now); Checkpoint and Circuit clocks count up, Blitz and timed
  lessons' down; the loading screens' tips include "In a Checkpoint
  Race, the finish line appears only after you've cleared the final
  checkpoint" and "Amateurs must place in the top 3 to unlock a race's
  characteristics", "Professionals must place first".
- **Still not seen**: idling's music (the runs never stop long), the
  pause menu, a drawbridge moving, the CD player, low-detail roads
  swapping in, sidewalk and divider ends close up.
