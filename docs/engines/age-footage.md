# Midtown Madness 2: what the game's own screens show

Some things the game's files don't say (where its HUD goes, how its
damage meter moves) can be seen on its screens. Three public gameplay
videos were watched for this; nothing from them is in Reluva but
measurements and observations, written down here.

| Video | Length | Picture | What's in it |
|---|---|---|---|
| "Midtown Madness 2 Walkthrough" | 5 h 55 min | 640 x 480, 30 a second | Crash Course from the first lesson to the final exam, many Blitz, Checkpoint and Circuit races in both cities, the Vehicle Showcase cars cruising, the in-car views, the options screens |
| "Midtown Madness 2 (2000) - PC Gameplay" | 34 min | 1280 x 720 (the game at that size), 60 a second | Circuit and Checkpoint races in both cities, at night, in rain and fog |
| "Midtown Madness 2 (2000): Old Game PC in 4K 60FPS" | 5 min | 1280 x 720 as fetched | Cruising San Francisco, a few races |

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

- When the bridges open, the music's rules for changing segments, the
  props' Flash values and the camera's numbers beyond what's in
  [age-driving.md](age-driving.md): nothing in the videos shows them
  clearly enough to measure.
- The opening movie isn't in any of them.
