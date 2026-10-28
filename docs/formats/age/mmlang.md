# mmlang.dll strings

The game's text: `mmlang.dll` in the install folder, a Windows DLL whose
only resources are string tables (42 of them, 667 strings, in English in
the retail release). Read by `src/common/pe.cpp` (`readPeStrings`) and
`src/engines/age/lang.cpp`; nothing in it is run.

## How this was worked out

A DLL's resources are laid out as Microsoft's PE/COFF specification
describes: the optional header's third data directory points to a tree
of type, name and language. Type 6 is string tables: each table (its
name n) holds 16 strings, ids (n - 1) x 16 to n x 16 - 1, each a 16-bit
length and that many UTF-16 characters, empty ones length 0. Read that
way, every table of this one comes out as text.

## What Reluva uses

| Ids | What |
|---|---|
| 532-544 | London's Crash Course lessons, in `crash0data.csv` ... `crash12data.csv`'s order: London Leap, Cutting Corners, The Weaver's Tale, Midterm 1, The Knowledge, Round You'll Go, Follow That Car!, Midterm 2, Give Me A Brake!, River Dancing, Artful Dodging, Midterm 3, Final |
| 545-557 | San Francisco's: Frequent Flyer, Hit or Miss, Turn It Up!, Midterm 1, Brake Dancing, The Heat Is On, Bonnie & Collide, Midterm 2, Keep It Clean, About Face, Straight, With Chaser, Midterm 3, Final |
| 192-245 | the lessons' messages, in groups by kind (a group starting with a lesson's name, then its messages: "Take the jump to hit all checkpoints", "Maintain %.0f through the checkpoints", "Wait...5 second penalty"...), read for the lessons' rules ([age-racing.md](../../engines/age-racing.md)) |
| 651 | "Pass" |
| 158-164 | Blitz: "Ready...", "Set...", "Go!", "Time's up!", "Game over!", "You've beaten all the Blitz races!!", "You Won!" |
| 165-178 | Circuit: "Ready...", "Set...", "Go!", "Wait...5 second penalty", "You finished 1st!" to "You finished 8th", "You are a loaf", "You've beaten all the Circuit races!" |
| 179-191 | Checkpoint: "Ready...", "Set...", "Go!", "Game over!", "You finished 1st!" to "8th", "You are a loaf." |
| 62 | "Final lap!" |

Which city's names are which was matched with the lessons' files: the
first of 532's group is London Leap and London's `crash0` is its
`longjump`; the names follow the curriculum on the Crash Course
backgrounds (`jpg/ccsf_bk`, `cclon_bk`). Some strings end a line inside
(a newline character). Font names ("Gill Sans MT, 10, 16, 0, 400") are
among them; the rest is the menus' and multiplayer's text (and earlier
copies of the races' groups, 89-157, for the network races), not used
yet. Which race group is which kind follows from their last lines
("You've beaten all the Circuit races!").
