# SimCoaster: how scripts run

> **Status:** partial · **Code:** `src/engines/tpw/script/machine.cpp` · **Checked against:** the five script sources the game ships, and all 288 scripts run · **Confidence:** inferred unless marked

## Overview

Each ride, shop and feature runs its compiled script ([rse.md](../formats/rse.md))
on the game's script machine, alongside the park. A script keeps its own
variables, flags, stack and place in its code; it asks the game to act
(an animation, a guest on, a ride's command) through instructions the
machine passes on; the game, in turn, writes some of its variables (a
guest waiting in `VAR_LETMEON`). The game's program can't be read, so
how the machine works is worked out from the five sources that ship
with the game (`Data/TestScript/`, with their comments), from the 288
scripts' own code, and from OpenTPW's written pages, and checked by
running every script.

## How it works

### Turns

A script runs on from where it was until it gives way, then the others
have their turn:

- `ENDSLICE` gives way: every script's main loop has one, or a wait.
- `WAIT n` gives way for n milliseconds: its values (200, 500, 1000,
  3000 in hundreds of places) and `test.rss`'s "Set the timeout" of
  `ADD VAR_STARTNOW 10000` on `GETTIME` read as milliseconds. `WAITABS
  n` waits so too: the bridge obstacle loops on `WAITABS 10000` and
  three animations, the burning vegetation spreads `WAITABS 300` apart;
  "absolute", perhaps, by the clock rather than the park's time when the
  game runs fast *(guessed)*.
- An instruction that waits for its animation (`WAITANIM`) gives way
  until it ends.
- A script that runs its time slice of instructions gives way: the
  header's 50 *(guessed)*.

`CRIT_LOCK` and `CRIT_UNLOCK` don't keep the turn: the shops spin
inside one (`.a27 TEST VAR_LETMEOFF; BRANCH_NZ a27`, waiting for the
game to take a guest off), which they could never leave if the section
kept the turn. The game, it seems, leaves the script's variables alone
inside one: "do not disturb", `test.rss` says of it.

### Variables, numbers and flags

Every variable starts at 0 ("All variables are initialised to zero by
default", the sources say). Arithmetic puts its result first:

- `SUB result a b` is a − b: the bus's `SUB VAR_TEMP VAR_TEMP 3000`
  has a number last, so it can't be the result's place; read so, the
  sources' `SUB VAR_TEMP VAR_CAPACITY VAR_TEMP` is the room left and
  `SUB VAR_TEMP VAR_STARTNOW VAR_TEMP` the time left, as their comments
  say. `DIV`, `MOD` and `MULT` the same (`DIV VAR_TEMP VAR_CAPACITY 9`).
- A result given a number for its place is dropped, its flags kept: the
  tour ride's `MOD 0 VAR_CAPACITY 9` before `BRANCH_Z`.
- `ADD v n` adds n to v (`ADD VAR_SPACELEFT -1`, then `BRANCH_Z go ;
  Full yet ?`).

Two flags, zero and negative, are set by `TEST`, `CMP`, `ADD`, `SUB`,
`MULT`, `DIV` and `MOD`, and by the
game's answer to the instructions that are straight followed by a
branch somewhere in the scripts: `TOUR`, `BUMP`, `COAST`, `WALKGET`,
`INLIMBO`, `UNLIMBO`, `FORCEUNLIMBO`, `LIMBOSPACE`, `BOUNCING`,
`GETANIM_CH`, `GETTIMER`. `BRANCH_Z` and `BRANCH_NZ` test zero,
`BRANCH_NV` negative, `BRANCH_PV` above zero: the vegetation waits out
its timer with `GETTIMER VAR_TIMER; TEST VAR_TIMER; BRANCH_PV`, which
would never end if 0 counted, and `test.rss` counts a ride's laps down
with `ADD VAR_COUNT -1; BRANCH_PV runlp`, `VAR_DURATION` laps so. A
slice leaves the flags as they were: `test.rss` tests, gives way
(`ENDSLICE`), then branches.

`CMP a b` sets the flags as `SUB` would, a − b: after `RAND VAR_TEMP
10; CMP VAR_TEMP 4` the scripts branch three ways (`BRANCH_PV`, then
`BRANCH_NZ`, then on), which an AND (as OpenTPW's page has it) couldn't;
the litter bin catches fire past its risk, `CMP VAR_CONTENTS
VAR_FIRERISK; BRANCH_PV`. `RAND v n` puts a number from 0 to n, both
ends, in v: the camel race picks among its four lanes with `RAND
VAR_TEMP 3`, then tests for 0, 1, 2 and 3; the three-lane games roll 2.
(The litter bins' `RAND 600 VAR_TIMER` has its operands the wrong way
round, so it rolls nothing: a slip in the game's own scripts.)

### Calls, the stack and children

`JSR` and `RETURN` call and return; `PUSH` and `POP` use a stack of the
header's size. `SPAWNCHILD` and `SPAWNSOUND` start a script from the
same folder, which runs after its parent each turn: the rides' sound
scripts (`EventMap.rse`) and the end feature's (`AnimCtrl.rse`).
`SETVARINCHILD`, `GETVARINCHILD`, `SETVARINPARENT` and `GETVARINPARENT`
reach across. `GETTIME` is the time since the script started. `END`
ends it.

### The game's part

Everything else acts on the park: animations, objects, guests (limbo,
bouncing, walking), screams, effects, the ride's own commands (`TOUR`,
`BUMP`, `COAST`), each in
[script-instructions.md](script-instructions.md). The sources show some
of it: a shop takes the
guest in `VAR_LETMEON` into limbo for `VAR_DURATION`
(`LIMBO VAR_LETMEON VAR_DURATION`), counts those inside (`INLIMBO`),
and lets the next out into `VAR_LETMEOFF` when their time is up
("get next person out (if any)", `UNLIMBO`); the game clears
`VAR_LETMEOFF` as the guest leaves.

## In Reluva

`ScriptMachine` (`src/engines/tpw/script/machine.cpp`) runs a script as
above; the game's instructions go to a `ScriptHost`. Until the park
runs, `SimulatedHost` stands in for it, as
[script-instructions.md](script-instructions.md#in-reluva) says: a
clock, guests coming into the queue and kept as the instructions say,
the ride's capacity and duration from its settings, animations as long
as the model's own of their number and variant, where known
([md2-animation.md](../formats/md2-animation.md#which-animation-is-which)),
or a second; and a trace of every instruction for the game.

`reluva-tool tpw rse-run <game folder> <script> [seconds] [-guests]`
runs one and prints the trace and its variables. `reluva-tool tpw
rse-check` runs every script for a simulated minute with guests: all
288 run it without stopping (173 still running, 115 waiting), the
game-files test checks it (`tpw.gamedata.scripts`). The viewer's
Scripts page runs one with R, its variables and trace shown; the
Models page runs a model by its script with R, posed by the animation
the script last started (`WAITANIM`, `TRIGANIM`, `TRIGWAITANIM`,
`LOOPANIM`; `FLUSHANIM` stops it, `WAIT4ANIM` waits for what is left
of it). The unit
tests (`tpw.unit.script`) run scripts built in the test.

## Sources and checks

- **Where the facts come from:** the five shipped sources and their
  comments; the 288 scripts' code (which operands are numbers, which
  instructions come before branches); OpenTPW's written pages, for
  `CMP` and the ride commands' numbers. Each rule above says which.
- **How it was checked:** every script run for a simulated minute. The
  first rules tried stopped 20 of them, each a wrong guess the scripts
  corrected: the shops' spin inside a critical section, the bus's and
  the tour ride's operand order. The shipped `Shop.rss` traced with
  guests does what its comments say: three guests inside at most, each
  for a second.

## Open questions

- The time slice: instructions, milliseconds or something else.
- What `WAITABS`'s "absolute" means, if anything but `WAIT`; `PUSH` and
  `POP`, which no script uses.
- How the game runs a script's turns against its own: how often, in
  which order.
- The game's instructions still open
  ([script-instructions.md](script-instructions.md#open-questions)).
