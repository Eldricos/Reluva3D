# Acts, scenes and the game's start

The story of *Murder on the Orient Express* is in acts: `01A` to `09A`.
Each act has a row in `Text/acts.txt` that says what loads and what runs
when it starts. This page covers how an act starts, how scenes change,
and the state that goes with them: timers, time of day, weather, and the
train.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the game object's act and scene code, `0x422470..0x423d40`;
- the runner's `act`, `scene`, `savescene`, `timer` and `time` verbs;
- scene entry, `0x4362b0`, and the evaluator's `timer`, `beenhere` and
  `lastscene` words.

The results were then checked against `acts.txt` and the 394 scenes of
the English install.

## acts.txt

The table is tab separated, with a header row. Its columns are:

| Column | Read as |
|--------|---------|
| Act | `01A`. A name splits into a number (everything but the last character) and a letter (A = 1). |
| PreLoad | A script, run when the act starts (`fade out, movie teaser, movie intro`). |
| Location to Load | A folder of `Locations` (`sirkeci.market`). 06A, 06B and 07A have `calais.hall ` with a space after it, which Windows drops from a folder's name. |
| Post Load | A script, run once the location has loaded. It normally shows the first scene (`scene sirkeci.market_01 1 0, fade in, add ticket`). |
| Time of Day | Night (1) if the cell contains `night`, otherwise day (0). |
| Weather | 2 if it contains `blizzard`, 1 if it contains `snow`, otherwise 0. |
| Train Moving | 1 if it contains `yes`. |
| Completion Conditions | A [condition](ac2.md#conditions). |
| Event | A script, run when the condition holds. |
| Music | `sound\music\<cell>`. Every cell is `0`, so the column is unused. |
| What to do | Notes; the game doesn't read them. |

Cells may be quoted, with doubled quotes inside. A cell of `0` counts as
empty.

## `act <act>`

The `act` verb changes the act, unless its argument is shorter than two
characters. A bare `act` starts the current act again.

When an act starts, the dialogue tables are always reloaded: the
`gen_` tables plus the act's own `act<NN><x>.txt`.

If `acts.txt` has no row for the act, nothing else happens, and the
previous act's row stays in force. `act 01b`, 01A's Event, works this
way.

If there is a row:

1. The act's PreLoad is queued on the main script runner.
2. Once the main runner is idle (PreLoad has run, along with anything
   queued before it), the game leaves the current scene and loads the act's
   location behind a loading screen. The picture is
   `Interface\loading\loading_<place>.jpg`.
3. The act's Post Load is queued.

Scenes are kept once loaded, with a few exceptions:

- Before act 04A and from 08A on, moving to a new location drops every kept
  scene, because each location is a separate place.
- Inside the train (04A to 07A), only the scenes loaded on demand are
  dropped (those whose `LOAD` record is 0).

The act's time of day, weather and train state take effect when a scene
is left. The condition words `time`, `weather` and `moving` test them.

The Completion Conditions are checked each time the main runner's queue
runs dry. When they hold, the Event is queued. Only three acts have
conditions:

- 04A: `tag act04a >1, tag eavesdrop04a >4`.
- 05A: `tag act05a`.
- 05C: the ashes puzzle's two tags.

The other acts are moved on by `act` commands in scene scripts, dialogue
cells and Post Load cells.

## The game's start

When the program starts, it queues three commands:
`musica motoetheme`, `movie logos inscene` and `scene menu.main_01 1 0`.

The main menu's scene event is `reset`, which clears:

- tags, including the text tags;
- timers;
- the inventory.

It also sets the act to 01A. The menu's Play button runs a bare `act`,
which starts 01A. 01A then runs:

- PreLoad: the teaser and intro movies.
- Location: `sirkeci.market`.
- Post Load: `scene sirkeci.market_01 1 0, fade in, add ticket, add
  passantoinette`.

Market_01's scene event then plays the opening.

## Scenes

### `scene <name> [<walk point>] [<flag>]`

The walk point defaults to 1, and so does the flag.

The game looks for the act's variant of the scene, `<name>_<act>.ac2`
(`salon_01_06a`), and falls back to `<name>.ac2`. A variant is loaded on
top of its base scene (`0x435990`, mode 2): the base's camera and
settings stay, even where the variant has its own (`Calais.14-15_14.02`
in 06A and 06B); the variant's music and events are added; its objects
are added unless the base has the same one (the same tag, file and
place; the player by its file alone). If the scene isn't in the current
location, the location is loaded first, behind a fade.

### Leaving a scene

- Where the characters stood is kept, because the scene itself is kept.
- The act's time, weather and moving take effect.
- The scene's path becomes `lastscene`.

### Entering a scene

1. The player and a following character are placed at their walk point
   with the scene's number, or failing that at their first walk point.
   With -1, a kept scene leaves them where they were.

   **Walking in.** With a non-zero flag (the default), they then walk in
   (`0x440808`):
   - they play `walk` and go straight ahead along the walk point's
     heading, 100 units for the player and 50 for a follower;
   - there is no path-finder, and they move at the walk's own pace;
   - they end in `idle1`, eased in over 0.5 s.

   Nothing waits for them: the scene's events run meanwhile. No script
   writes the flag as 1; `scene <name> <n> 0` turns walking in off, and
   `previous`, `gotoscene` and loading a save enter with −1 and 0.
2. Music: every `MUSC` entry whose condition holds plays, looping. Sounds
   that were playing stay on if the new scene also names them; the others
   stop.
3. Events: the first `EVNT` entry whose condition holds is queued, and only
   that one.
4. `PUZZ` opens the scene's puzzle or menu screen.
5. The visit is noted. A text tag named after the scene's file
   (`Salon_01_06a.ac2`) gets the act added to it. This happens after the
   events, so `!beenhere this` holds during the first visit's event.

### `savescene` and `gotoscene`

`savescene this|<scene> [<wp>] [<flag>]` keeps `<file> <wp> <flag>` in
the text tag `savedscene`. The walk point and flag default to -1 and 0.
Scripts use it before going to a closeup.

`gotoscene` runs `scene` with those words, taking `.ac2` off the name.

## Timers and conditions

| Command or condition | Effect |
|----------------------|--------|
| `timer <name>` | Adds a timer at 0. A second one of the same name can be added; the first is the one that counts. |
| (every second) | Each timer goes up by 1, stopping at 32000. |
| `time <n>` | Adds `n` to every timer: the radio's `time 60` makes `timer hamtimera >60` hold. |
| `timer <name> [<\|>n]` | Compares a timer (-99 if there is none). Without a number, holds if the timer exists. |
| `beenhere`/`beenthere <scene\|this> [<act>\|ever]` | Whether the scene was visited in this act, in that act, or ever. |
| `lastscene <name>` | Whether the scene left last was that one. |

## In Reluva

`Story` (`src/engines/awe/story.cpp`) handles the acts: it reads
`acts.txt`, starts acts, loads their locations, and checks their
completion. It also ticks the timers. `ScriptRunner` does `timer`,
`time`, `reset` and `savescene` itself.

In the Scenes page, **N** starts a new game: the main menu, then Play.
The page then does the following:

- Loading a location works as the game's load state machine does. The
  location's scene files (the act's variants in place of their bases)
  are read one per frame behind the loading screen: the location's
  picture, with the wheel turning at its foot, faded in if the screen was
  faded out and faded out at the end. An act's Post Load then runs. A
  `scene` in another place fades out, loads, then shows the scene and
  fades in, and the event waits until then. Scenes themselves are read
  again when shown, so loading a new place only forgets how the scenes
  of the earlier one were left.
- Entering a scene plays its music and queues its event.
- `scene` and `gotoscene` keep the player's place for walk point -1.
- `fade` takes half a second. This is a guess.
- Movies play full screen with their subtitles, and Escape skips them
  (see [script.md](script.md#movies)).
- `act` and every cell of `acts.txt` are handled. The Event of 08A,
  `act09a`, isn't a command (no space), but 08A has no conditions, so it
  never runs; 09A is a developers' test act (its "What to do" says so).

## Playing the acts headless

`reluva-tool awe-acts <install> [act|slotN...]` plays without a window
and without a player. Each act is started as `act <x>` starts it (its
PreLoad, its location, its Post Load), or each developers' save is loaded
(`preload slotN, load`). Then the game runs on, with movies skipped,
lines cut short and choices taken (each time the one taken least so far,
so a quiz's wrong answers are all tried), until nothing runs. It lists
the commands Reluva doesn't do, scenes it can't find, and events that
never end. `-hotspots` then clicks every hotspot and character of that
scene (and uses on it the items it asks for), each from the start again;
`-explore n` plays on for n clicks (or items used, or items combined in
the inventory where `combinelist.txt` has a row for them), each the one
tried least in its scene. `-v` prints the whole script log. A
developers' save is loaded as the load screen loads one (`preload slotN,
load, fade in`).

With no arguments it plays every act and every developers' save; on the
English install that takes seconds, and finds nothing Reluva doesn't do.
What each act needed, and where the explorer got to in 600 clicks:

| Act | What it does | Found and fixed |
|-----|--------------|-----------------|
| 01A | Sirkeci market: the opening, then trading for the tile, the bowl and the parasol | The explorer stays in the market (the trades and quizzes need answers it doesn't find). From the save in Sirkeci's hall (slot 1) it plays on through 02A and 03A into 04A. |
| 02A | Dinner, a conversation of six parts, then `act 03a` | Plays through. |
| 03A | Belgrade station | Reaches 04A. |
| 04A | The Calais coach at night: the water glass, listening at doors; ends when `tag act04a >1, tag eavesdrop04a >4` | Listening at doors needs `(use waterglass \| tag autoeavesdrop nouse)`: brackets and `use` in conditions. |
| 04B, 04C | Peeping out of the door | Each goes on to 04D. The dialogue's `terminate` and the like had been logged as not done. |
| 04D | The scarlet kimono | The woman is clickable only during `unlock, wait 6.25, lock`: the runner's lock. |
| 05A | The challenge; ends on `tag act05a` | Reaches 05B. |
| 05B, 05C | The murder scene; the ashes puzzle | The fingerprint cards and passports (`startprint`, `startpass`). |
| 06A, 06B | Restoring the heat; evidence | Dragomiroff's room in 06A and 06B was a dead end: its variant has a camera, so its base's door was lost. `cantalk` decides between conversations. |
| 07A | Poirot thinks; Barnaby's radio (`timer`, `time 60`) | From the developers' last save (slot 14) it plays on through 08A's three solutions, the coda and the credits to the main menu. |
| 08A | The three solutions, as quizzes | `(!tag failsuspects), tag winsuspects`: a condition in brackets in a script. |
| 09A | A test act in the forest | Runs. |

Throughout: `gen_characters.txt`, every character's generic lines and
goodbyes, had been skipped (its first heading is a name, and Reluva read
the tables by their headings); and missing tags compared as missing
rather than as -99.

### A playthrough from a new game: `-solve` and `-play`

The explorer above clicks at random, and gets stuck where the game wants
a player who knows what to do: 01A's market, 04A's eavesdropping, 04D's
kimono, and the evidence of 06B and 07A. `awe-acts <install> -solve
<out.txt>` searches instead for a way from a new game (the main menu,
Play) to the end credits, and `awe-acts <install> -play <out.txt>` plays
one back from a new game. On the English install the search took about
an hour (most of it in 06B and 07A) and found a way of 1,086 actions,
which plays back in a minute and reaches the credits.

**The playthrough** is a text file, an action a line: `new` (a new game),
`click|<scene>|<object>|<n>|<choices>` (the n-th clickable of that name in
that scene), `use` (the same, with an item in hand), `combine`,
`decombine`, `flip` (an item's back looked at, its Examine Event),
`compare` (two fingerprint cards under the magnifier), `travel` (a car of
the bar), `solve` (a puzzle screen). `<choices>` are the dialogue's
choices made while the action ran (`2.1.1`), and a last field
`<object>#<n>` clicks that object as soon as the game takes a click
while the action's script still runs. After each action the game runs on
till it settles: nothing running and nothing said for 3 seconds of game
time, movies skipped, lines cut short. The file names objects, items and
scenes only (no game text); `-play -list` shows what could be done at
its end, `-log n` the script log from the n-th action on.

**What a player can do** in a state (`Player::actions`, the Scenes page's
tool functions in `scene_autoplay.cpp`): click each hotspot, character
or picture that has a script and whose condition holds; use on it each
item the cursor would show it takes (`0x419e90`: a conditional script
asking for it that holds with it in hand, or a `uselist.txt` row), on
Antoinette too; combine the items of each `combinelist.txt` row that can
be combined, take apart those that can be (`Can Decombine`); turn over
each item with an Examine Event; compare each pair of fingerprint cards
the magnifier would match (a thing's card whose Flipped Description names
the person's); travel to each car the bar offers; solve the puzzle shown.
The puzzles are solved as the mouse would solve them, worked out from the
puzzle's own checks (`ScenePage::puzzleStep`): the box's next correct
move (`boxMoveCorrect`) clicked on its piece and arrow; each piece of the
torn letter picked, turned upright with the arrows and dragged to its
cell; the safe's 6-6-6 entered a press and a release at a time, then its
handle. Dialogue choices are a player's too: an action is first played
with the choice taken least in it at each point (so a conversation's
topics are all heard), then again with each other answer at each of its
first eight choice points.

**The search** branches by saving the game to memory in the game's own
format and loading it again (`saveToMemory`, `loadFromMemory`), as the
save and load screens do; the scene left last goes with it, as a saved
game doesn't keep it and `lastscene` is tested (the closets show the shoes
of the compartment come from). States are told apart by the act, the
scene, the scene left last, the tags, the items' states and the
conversations' progress. From the root, states are taken best first:
those with something new to their scene (a story tag, an item, the scene
come from: not the conversations' progress, nor the "met them here"
tags) before those with nothing new, then those where an item can be
used, then the furthest on. A state with something new to the act's story
(a story tag, an item's state or a conversation's progress never had in
the act; of a tag's value only if the game compares it, `tag alibis >9`,
as the conditions tested so far show) becomes the new root: an enforced
hill climb. A root that leads to nothing new within 5,000 states is a
dead end, and the search goes back to the root before. A new act starts
afresh. Two things keep it quick: an action that changed nothing before
is skipped where every clause of the conditions it had tested
(`GameState::onTest`) comes out the same, and the inventory's actions and
travel are tried once a story state, not in every scene.

What each act needed (actions in the playthrough found):

| Act | Actions | What it took |
|-----|---------|--------------|
| 01A, 02A | 41 | The market's trades and quizzes, by trying each answer; Michel and Poirot in the hall. 02A plays on by itself. |
| 03A | 32 | Belgrade's platform and the engineer. |
| 04A | 106 | The water glasses from the Salon car's bar, listening at the doors (with the glass in hand once, then by itself: `tag autoeavesdrop nouse`), Michel locking the baggage car; the puzzle box on the way. |
| 04B, 04C | 2 each | Back to bed. |
| 04D | 24 | The kimono: the woman clicked between the scene's `unlock` and `lock`. |
| 05A, 05B | 14 | The murder scene and Constantine. |
| 05C | 74 | The hat frames, the burnt paper held in the curling tongs, the spirit lamp from the restaurant lit with the matches, the ashes puzzle. |
| 06A | 203 | The engineer's hammer (for his teeth), the snowshoes from the baggage car's crate, put on Antoinette in the forest, the wolves, the hut's padlock and its stove pipe, the fireman's gloves; the torn letter on the way. |
| 06B | 427 | Every passport and fingerprint card, the prints compared, thirteen pairs of shoes compared in the closets, the safe, the suspects' timetables and alibis; then Poirot. |
| 07A | 159 | Barnaby's radio: its battery built in the radio (the voltaic cell the inventory makes is no use there, and is taken apart again), the questions sent and, after `time 60`, the answers; the kidnapping; then Poirot. |
| 08A | 1 | The three solutions, with the choice taken least, to the credits. |

Found in Reluva on the way, and fixed as the game does it:

- **Items used on Antoinette.** The game looks `uselist.txt` up for the
  player's character as for the others (`0x41c0bb` when an item is used,
  `0x419ec7` for the cursor: `PCXX` or `NPCX`); Reluva only did it for the
  others, so the snowshoes couldn't be put on.
- **A conversation's extras and threads count only with a line to offer**
  (`0x43a470`, `0x43a9d9`; see [dialogue.md](dialogue.md)). A character's
  fingerprint offer whose first line's condition failed kept a heard part
  going, so `talk dia_hubbard a+` never got to part b.
- A new game closes any puzzle or options screen still open.
- A Scenes page that doesn't draw (a tool's) no longer copies the screen
  each frame, decodes video loops or moves particles, and the dialogue
  tables are read once: the search is ten times faster for it.

And in the tool: the dialogue's choices are asked for only when the game
would take one (the box slid in), not every frame.

No walkthrough of the game was consulted: where the search stuck, the
scenes' scripts and the dialogue tables said what was missing, and the
tool was made to find it.
