# Star Trek: Armada: mission scripts

> **Status:** partial · **Code:** `src/engines/storm3d/play/mission_scripts.cpp` · **Checked against:** Federation 1's scripts started in Reluva's x86 emulator · **Confidence:** seen, not traced unless marked

## Overview

Each campaign mission's events (its cinematics, objectives, waves of
enemies, winning and losing) are compiled code: two Windows DLLs in
`missions/`, named for the mission, `<name>S.dsl` and `<name>R.drl`
(`Federation1S.dsl`, `Federation1R.drl`), 30 pairs. Reluva runs them
in its x86 emulator, as the original program loads them, standing in
for what they take from Armada.exe.

## How it works

- **The pair:** the `S` DLL holds the mission's variables and steps,
  all exported by name (`?Enterprise@@3HA`, a unit's handle;
  `?MISSION_BEGIN@@3Vhot_int@@A`; `?win_mission@@YAXXZ`). The `R` DLL
  imports them from it, so it's loaded second, and holds the rules:
  each a condition and an action, made through the world's rule engine
  (`IIIE::create_rule` on `WORLD`, its engine 0x38 into it) and tied to
  the variables it depends on (`hot_type::add_dependent_rule`)
  *(from the code)*.
- **Start-up:** neither exports a function to call; both do their work
  in their C runtime's start-up, as their global objects are built.
  Federation 1's builds 17 `hot_int`s and 12 `timer_int`s, gives each
  its first value, looks up 12 units by name (`GetHandle`), and makes
  34 rules tied to 52 variables, among them the program's own
  `ALWAYS_EVALUATE`, `isCineractiveDone` and `isCineractiveCancel`.
- **What they import:** Federation 1's pair takes 94 functions and
  variables from Armada.exe: the rule engine's classes (`hot_int`,
  `timer_int`, `dirty_int`, `rule`, `IIIE`, `script`, `character`,
  `linked_list`) and the game's functions (`Goto`, `Attack`,
  `BuildObject`, `CineractiveShot`, `ShowTitle`, `SucceedMission`...);
  all 30 missions, 178 functions besides the engine's. Their own C
  runtime is linked in: from Windows they take KERNEL32 alone.
- **Calls:** the names are Visual C++'s decorated ones, which give each
  function's arguments and how it's called: `@@YA`, a function the
  caller pops the arguments of (cdecl); `@@QAE` and `@@UAE`, a member,
  `this` in ECX, popping its own (thiscall); `@@3`, a variable.

## In Reluva

- **`MissionScripts`** loads the pair into an emulated process
  (`Armada.exe`, 64 MB), the `S` DLL first, and runs their start-up.
  Each Armada.exe import is stood in for by what its decorated name
  says of its arguments: a function goes to the `game` callback by its
  plain name (`GetHandle`, `hot_int::set`), its arguments read as the
  script passed them; a variable gets room of its own. Their C
  runtime's thread-local storage is a table of slots: they run on one
  thread.
- **`reluva-tool storm3d mission-scripts <game folder> <mission>`**
  loads a mission's scripts and lists each call they make of the game.

## Sources and checks

- **Where the facts come from:** the DLLs' import and export tables,
  their disassembly (the `R` DLL's rule set-up at 0x100025a0 in
  `Federation1R.drl`), and Armada.exe's exports.
- **How it was checked:** `mission-scripts` on Federation 1: both
  DLLs start without a fault, the calls listed above.

## Open questions

- The rule engine's cycle: when rules are tested and fired, and how
  timers count; then the game functions, one by one.
