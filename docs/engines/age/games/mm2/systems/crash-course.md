# Midtown Madness 2: Crash Course

> **Status:** done · **Code:** `src/engines/age/game/lesson.cpp` · **Checked against:** recordings · **Confidence:** inferred unless marked

## Overview

Each city's 13 driving lessons, and how each is judged. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

Each city's 13 lessons (`race/<city>/crash<n>data.csv`,
[race.md](../formats/race.md)), named in `mmlang.dll`
([mmlang.md](../formats/mmlang.md)): San Francisco's "Golden Gate
Stunt Driving School" (Frequent Flyer, Hit or Miss, Turn It Up!, Midterm
1, ... Final) and London's "East End Cab Company School of Driving"
(London Leap, Cutting Corners, ...). A lesson is driven through its
checkpoints (its own file's), in its row's time limit, from the first
point; its time of day and weather are `mmcrashdata.csv`'s row; its
traffic is its own row's AmbDensity; the car to follow (or drive among)
is its aimap's opponent.

What each lesson asks isn't written down beside it: the event number
picks a group of `mmlang.dll`'s messages (ids 192-245), and Reluva reads
the rules from those:

| Event | Lessons | Messages | Reluva's rule |
|---|---|---|---|
| 0 | Frequent Flyer, London Leap, River Dancing | "Take the jump to hit all checkpoints", "Good jumping!" | every checkpoint in time |
| 2 | Straight, With Chaser, Follow That Car!, Midterm 3 (SF), Final (London) | "Follow that car!", "You Won!", "Car escaped!" | stay within 120 m of the car (its aimap opponent) until it's at its end with you near; farther, "Car escaped!". Untimed, with no clock: in the footage Straight, With Chaser (TimeLimit 36) runs 88 s and Midterm 3 92 s before "You Won!" |
| 3 | The Heat Is On, Artful Dodging | "Practice evading your pursuers!", "Lose your pursuers before you finish!", "You survived the gauntlet!" | all of the aimap's police chase from the start; the end counts once none is chasing; busted fails |
| 4 | Turn It Up!, Cutting Corners, Midterm 1, Final (SF) | "Maintain %.0f through the checkpoints", "You have not maintained the minimum speed!" | every checkpoint at the row's first extra column (50 or 40 mph) or faster |
| 5 | Keep It Clean, Round You'll Go | "Don't even touch another car!", "You scraped the paint!" | touching a car (the traffic's or the police's) fails |
| 7, 9 | Hit or Miss, Brake Dancing, About Face, The Weaver's Tale, The Knowledge, ... | "Ready... Set... Go!", "Time's up!" | every checkpoint in time |
| 8 | Bonnie & Collide, Midterm 2 (SF) | "Destroy that car!", "You did it!", "Car reached destination", "Get going! Lose the tail" (616-623, 652) | ram the aimap's car until it's over or stopped 2 s (it isn't put back on its path) before it reaches its end or the time's up; no arrow till then. With police about (Midterm 2's six), then lose them to the checkpoint, as The Heat Is On |

Being busted or wrecked fails any lesson. The checkpoints' fifth column,
called "poly count" as in the circuits, varies from gate to gate here
(4-16) and is read as the gate's radius.

Event 8 had been read from the messages 210-216, which `mmlang.dll`
names "Bonnie & Collide" too ("Drive as cleanly as you can!", "Wait...5
second penalty"); but the lesson's picture says "Buckle up! It's time to
ram another car into submission!", the walkthrough footage shows the
pickup rammed until "You did it!" and Midterm 2 as "Destroy that car!"
then "Get going! Lose the tail", and 616-623 are that lesson's words. No
lesson uses 210-216.

The other extra columns: the third (1 in the follow and destroy lessons,
every lesson with an aimap car) marks the car, which the event already
says. The second (1 in Brake Dancing, "master the Forward 180", and The
Knowledge, "Race from landmark to landmark as you practice your map
skills") takes the compass arrow away: in the walkthrough footage The
Knowledge is driven from start to end without one (the Midterm before
it, with the column 0, has it throughout), the way found by the map.
The fourth (1 in San Francisco's Straight, With Chaser and Midterm 3)
isn't used: nothing in the files or the footage says what it does
(Straight, With Chaser plays as a plain follow; London's Follow That
Car!, with it 0, looks the same, neither showing a clock).

`race/sf/jump0`-`jump2waypoints.csv` are three short routes down San
Francisco's hills and along the bay (5, 6 and 4 points; `jump0.aimap`:
traffic 0.1, no police or opponents) and `ramp.pathset` one path,
"PATH01", of 13 points near `jump2`'s start, naming no model: nothing
names them as races or says what they're for, so they aren't used. A passed lesson offers the next
(Enter), any lesson its restart (R).

## In Reluva

`LessonJudge` (`src/engines/age/game/lesson.{h,cpp}`) judges a lesson
by its event number, with the rules in the table above, which are
Reluva's reading of the messages.

## Sources and checks

- **Where the facts come from:** `crash<n>data.csv`, `mmcrashdata.csv` ([race.md](../formats/race.md)) and `mmlang.dll`'s messages ([mmlang.md](../formats/mmlang.md)); public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** `age.unit.game`; the lessons played against the recordings.
