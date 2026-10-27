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
| Location to Load | A folder of `Locations` (`sirkeci.market`). |
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
top of its base scene. If the scene isn't in the current location, the
location is loaded first, behind a fade.

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
