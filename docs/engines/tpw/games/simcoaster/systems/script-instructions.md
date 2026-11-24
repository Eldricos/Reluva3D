# SimCoaster: the scripts' instructions

> **Status:** partial · **Code:** `src/engines/tpw/script/simulated_host.cpp` · **Checked against:** all 288 scripts run, and the five shipped sources · **Confidence:** inferred unless marked

## Overview

What each of the 70 instructions the game's scripts use does, and what
its operands are: numbers, variables it reads, variables it writes,
labels or strings. The machine's own instructions (arithmetic, branches,
waits, calls) are in [scripts.md](scripts.md); these are the ones it
hands to the game. The game's program can't be read
([executable.md](../formats/executable.md)), so each is worked out from
the shipped sources' comments, from the scripts' own code (what comes
before and after it, which operands are numbers, which variables are
tested after it), and from the files the numbers name (the particle
library, the sound categories, the models' animations).

The operands below: *n* a number, *v* a variable the instruction writes,
*g* a guest (a variable holding one, read), *e* an emitter (a helper of
the model, `-1` for none), *h* a handle the script gives a lasting effect
to stop it by.

## How it works

### Animations

| Instruction | Operands | What it does |
|---|---|---|
| `WAITANIM` | number, variant | Plays the animation and waits for its end |
| `TRIGANIM` | number, variant, *v* | Starts it and goes on; *v*, where a variable, gets its length in milliseconds: the bus waits that long less 3 seconds, then turns its engine up |
| `TRIGWAITANIM` | number, variant, 0 | Waits for the animation playing to end, then starts its own and goes on: the twister screams at once after it, and the helipad's `TRIGWAITANIM` is followed by `WAIT4ANIM` |
| `LOOPANIM` | number, variant | Plays it round and round |
| `WAIT4ANIM` | | Waits for what is left of the one playing |
| `FLUSHANIM` | | Stops it |
| `TRIGANIM_CH` | number, variant, 0, channel | Starts an animation on a channel of its own, which runs alongside: the shooting gallery's three lanes |
| `GETANIM_CH` | 0, channel | Answers whether the channel's animation is still going: the gallery goes on only when it answers below zero (done, it seems), past `BRANCH_PV` and `BRANCH_Z` |

The number is the animation's kind, its file's letter
([md2-animation.md](../formats/md2-animation.md#which-animation-is-which));
the variant picks among numbered files: the ATM plays `WAITANIM 5 0`
to `WAITANIM 5 14`, and has `atmm1.MD2` to `atmm15.MD2`; the camel race
plays its winner's lane with `WAITANIM 5 VAR_TEMP`, a roll of 0 to 3.

### Guests

| Instruction | Operands | What it does |
|---|---|---|
| `WALKON` | *g*, six numbers | The guest walks onto the ride, along a path the numbers choose: the camel race's lanes are `WALKON VAR_LETMEON 5 1 1 5 6 1` to `5 4 4 5 6 1` |
| `HUSH` | *g* | The guest is on the ride: "This guy is now on the ride", `test.rss` says |
| `ADDHEAD`, `DELHEAD` | *g* | Shows the rider on the ride, and takes them away as they get off |
| `HOP` | *v* | Takes the next rider off, into *v*: "Wait until AI has taken him", `test.rss` says after it, testing `VAR_LETMEOFF` until the game clears it |
| `WALKOFF` | *g* | The guest walks off the ride |
| `WALKGET` | *v* | Gives a guest who has walked off and is clear, into *v*; 0 (and the zero flag) while none is |
| `LIMBO` | *g*, milliseconds | Takes the guest out of sight for so long: inside a shop |
| `INLIMBO` | *v* | How many are in limbo |
| `LIMBOSPACE` | 0 | Answers whether there's room in limbo (its size is the script's `#setlimbo`): the balloon stall tests it before letting a guest in |
| `UNLIMBO` | *v* | The next guest whose time is up, into *v*: "get next person out (if any)" |
| `FORCEUNLIMBO` | *v* | The next guest, time up or not: when the shop closes |
| `BOUNCE` | *g*, milliseconds | A guest onto a bouncy ride for so long |
| `BOUNCING` | *v* | How many are bouncing |
| `UNBOUNCE`, `FORCEUNBOUNCE` | *v* | As `UNLIMBO` and `FORCEUNLIMBO`, for the bouncers |
| `BOUNCESETBASE`, `BOUNCESETNODE` | *n* | Set once at the start, on the bouncy rides alone: 8 to 20, and node 5: the bouncers' height and the model's node they bounce on, perhaps *(guessed)* |

A ride takes a guest from `VAR_LETMEON`, which the game fills with the
guest at the front of its queue, then clears it; it lets one off into
`VAR_LETMEOFF`, which the game clears as the guest leaves
([rse.md](../formats/rse.md#behaviour)). The game gives each ride's
script its capacity and duration (`VAR_CAPACITY`, `VAR_DURATION`), it
seems from its settings' `Upgrades[0].InitCapacity` and `InitDuration`:
the duration is a count of laps (`COPY VAR_COUNT VAR_DURATION`, then
`ADD VAR_COUNT -1; BRANCH_PV` round a lap).

### Effects: particles and sounds

| Instruction | Operands | What it does |
|---|---|---|
| `EVENT` | kind, *e*, number | Sets off an effect once |
| `ADDOBJ` | kind, *e*, number, *h* | Starts an effect that lasts |
| `KILLOBJ` | *h* | Stops it |
| `FADEOBJ` | *h* | Fades it out |
| `SETOBJPARAM` | *h*, 20, value | Sets a lasting effect's parameter: the bus sets its engine's 20th to 0, then 75 as it pulls away: its volume, it seems *(guessed)* |

The kind says what the number is:

| Kind | Number | Seen as |
|---|---|---|
| 1, 2 | A particle effect of `Particle/Tp2.plb` ([plb.md](../formats/plb.md)) | 1: the toilets' `Flies` and `GreenFumes`, the end feature's `Firework1` and `Firework2`, a sideshow's `SideShowWin`. 2: `Fire`, `Notes`, `FW1explosion` (the front end's). What tells them apart isn't known |
| 3 | A sound of the world's rides category (`levels/<world>/Sound/cat_rides`) | 371 of its 373 uses name one there (the other two in the Intro World, which has none) |
| 4 | A sound of the world's ambient category | 22 of 26 |
| 5 | A sound of a global category: rides, by `EVENT` (328 of 328) | The front end's `intro-doorclose`, the obstacles' `barriers` |
| 6 | Global kids | 54 of 54: the toilets' `wee1`, `flush` |
| 7 | Global staff | 6 of 6 |
| 8 | A global sound: ambient (the helicopter's `hel004`, the monorail's), rides (the fireworks' `fireW000`, `Mortar_1`) | 77 uses |
| 9 | Global ui | 11 of 11: the gates' `WinOneShot` jingle |

([sound-maps.md](../formats/sound-maps.md) for the categories.) `ADDOBJ
5` and `ADDOBJ 8` both take global sounds, ambient as well as rides'
(the water ride's `water000` by 5, the end feature's fireworks by 5 in
one world and 8 in another): what tells them apart isn't known. The
emitter is a helper of the model, numbered as the front end's are
([front-end.md](front-end.md#the-effects)); `-1` the thing itself.

| Instruction | Operands | What it does |
|---|---|---|
| `STARTSCREAM` | *g* (a count: `VAR_ONRIDE`), level | The riders scream, so many, so loud: `STARTSCREAM VAR_ONRIDE 20` as a ride starts |
| `SCREAMLEVEL` | level | Louder or softer: the twister goes 90, 20, 90 as it swings |
| `SINGLESCREAM` | count, -1 | One scream, as a ride breaks down |
| `STOPSCREAM` | | Stops them |
| `SETREVERB` | 0 or 1 | An echo, by its name: the twister sets it as it starts swinging; 6 uses |
| `DIPMUSIC` | 1 | Turns the park's music down under a jingle: the gates' and the end feature's |
| `REPAIREFFECT` | 1, then 0 | The mechanic's repair effect, on while a ride is mended |
| `SPAWNSOUND` | file | Starts the ride's sound script, `EventMap.rse`, as a child |

### Timers, rides and the rest

| Instruction | Operands | What it does |
|---|---|---|
| `SETTIMER` | milliseconds | Starts the script's countdown |
| `GETTIMER` | *v* | What's left of it, into *v*, 0 once done: the rides let a rider off each 350 ms (`SETTIMER 350`, then `GETTIMER 0; BRANCH_NZ` till it's out) |
| `TOUR`, `BUMP`, `COAST` | command, value | The ride's own motion, which the game moves (its models don't): the tour ride's cars, the bumper and go-kart cars, the coasters' trains. The commands' numbers: OpenTPW's pages name some (`TOUR 10`, a car in the station; `BUMP 5`, the track valid); some answer for the branch after them |
| `FINDSCRIPTRAND` | name, *v* | Finds a running script by its `NAME` (one at random, if several) into *v*: the bus finds "Traffic Lights" |
| `SETREMOTEVAR` | script, variable, value | Sets another script's variable: the bus turns the lights' variable 0 to 1 and back |
| `SPAWNCHILD`, `REMOVECHILD` | file / | Starts a child script (the end feature's `AnimCtrl.rse`), or ends it |

## In Reluva

`SimulatedHost` (`src/engines/tpw/script/simulated_host.cpp`) does what
these say on a stand-in for the park: guests in limbo, bouncing, on the
ride and walking off (clear after a second); each script's timer and
animation channels; the animations' lengths by their number and
variant, from the model's files; the ride's capacity and duration from
its settings. `TOUR`, `BUMP` and `COAST` answer only the checks above;
the effects, screams and music are only traced. `reluva-tool tpw rse-run
<game folder> <script> [seconds] -guests` runs a ride so: Arabian
Nights' `arabride.RSE` loads two riders, runs its three laps, lets them
off 350 ms apart and loads the next. In the park, its guests are the
park's visitors instead, offered one at a time from the ride's queue
([visitors.md](visitors.md#riding)).

## Sources and checks

- **Where the facts come from:** the five sources shipped in
  `Data/TestScript.wad` and their comments; the 288 scripts (each
  instruction's operands, what's tested after it); the particle library,
  the sound categories and the models' animation files, for the numbers.
- **How it was checked:** every script run for a simulated minute with
  guests, `rse-check`, none stopping; rides traced through a whole
  cycle; the unit tests (`tpw.unit.script`) run the guests', timers' and
  animations' instructions on scripts built in the test.

## Open questions

- The ride commands' numbers (`TOUR`, `BUMP`, `COAST`): what most do.
- `WALKON`'s six numbers; `BOUNCESETBASE` and `BOUNCESETNODE`.
- What tells particle kinds 1 and 2 apart, and sound kinds 5 and 8.
- `GETANIM_CH`'s answer while a channel plays: a frame, or time left.
- The 34 instructions no script uses ([rse.md](../formats/rse.md#the-opcodes)).
