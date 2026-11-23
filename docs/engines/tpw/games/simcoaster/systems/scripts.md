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
  `ADD VAR_STARTNOW 10000` on `GETTIME` read as milliseconds.
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

Two flags, zero and negative, are set by `TEST`, `CMP` (a bitwise AND,
OpenTPW's page says), `ADD`, `SUB`, `MULT`, `DIV` and `MOD`, and by the
game's answer to the instructions that are straight followed by a
branch somewhere in the scripts: `TOUR`, `BUMP`, `COAST`, `WALKGET`,
`INLIMBO`, `UNLIMBO`, `FORCEUNLIMBO`, `LIMBOSPACE`, `BOUNCING`,
`GETANIM_CH`, `GETTIMER`. `BRANCH_Z` and `BRANCH_NZ` test zero,
`BRANCH_NV` negative, `BRANCH_PV` above zero (`test.rss` counts a ride's
laps down with `ADD VAR_COUNT -1; BRANCH_PV runlp`; whether 0 counts as
positive is not settled).

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
bouncing, walking), lights, screams, events, the ride's own commands
(`TOUR`, `BUMP`, `COAST`). The sources show some of it: a shop takes the
guest in `VAR_LETMEON` into limbo for `VAR_DURATION`
(`LIMBO VAR_LETMEON VAR_DURATION`), counts those inside (`INLIMBO`),
and lets the next out into `VAR_LETMEOFF` when their time is up
("get next person out (if any)", `UNLIMBO`); the game clears
`VAR_LETMEOFF` as the guest leaves.

## In Reluva

`ScriptMachine` (`src/engines/tpw/script/machine.cpp`) runs a script as
above; the game's instructions go to a `ScriptHost`. Until the park
runs, `SimulatedHost` stands in for it: a clock, guests coming into the
queue and walking off, a limbo as the shop uses it, animations as long
as the model's own of their number, where known
([md2-animation.md](../formats/md2-animation.md#which-animation-is-which)),
or a second, "yes" to a ride's
checks of itself (a car in the station, its track valid), 0 to the
rest; and a trace of every instruction for the game.

`reluva-tool tpw rse-run <game folder> <script> [seconds] [-guests]`
runs one and prints the trace and its variables. `reluva-tool tpw
rse-check` runs every script for a simulated minute with guests: all
288 run it without stopping (182 still running, 106 waiting), the
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

- What most of the game's instructions do, and their operands.
- The time slice: instructions, milliseconds or something else.
- `BRANCH_PV` at 0; `CMP`; `WAITABS` (here: until a time from the
  script's start, *(guessed)*); `POP`'s operand-less result.
- How the game runs a script's turns against its own: how often, in
  which order.
