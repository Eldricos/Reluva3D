# Unconfirmed

Points in Reluva's Elite Force support that are assumed, inferred or
unknown rather than confirmed against the original game. Each says what's
uncertain, and how it could be settled where that's clear. Most don't
change what the player sees; the ones that might come first in each
section.

Confirming one usually means finding the code that uses it in the
disassembly, or comparing with the original game in play (see
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)). When a point is settled,
update the doc it comes from and remove it here.

## Releases and installing

- **Other releases.** Only the English Windows CD, its patches and the
  Expansion Pack have been seen. Still unseen:
  - the Mac release;
  - the German and French releases;
  - 1.0 and 1.1 servers' network protocol.

  Their files' signatures (`reluva --hash`) and any differences in data are
  needed.
- **Wise installers.** The format is described only as far as Reluva reads
  it, and checked on one installer (the 1.2 patch). The header and the
  install script aren't read. See [wise.md](../../common/wise.md).

## Rendering

- **macOS.** The Metal path passes the render check on GitHub's Mac
  runners, but hasn't been played on a Mac.
- **Map rules taken from Quake III.** These were read from Quake III's
  renderer, not Elite Force's:
  - how bad lightmap and fog indices are corrected;
  - how a light grid of the wrong size is ignored.

  Elite Force's engine may differ. See [bsp.md](formats/bsp.md).
- **Lightmap brightness.** The original doubles or quadruples lightmaps
  depending on its gamma mode. Doubling is assumed, which matches its look
  in a window.
- **Holomatch's effect entities.** Some drawn types are inferred from what
  they look like and how they're used:
  - type 13 is taken to be a ring or cone. Its field at 144 is taken as
    texture repeats, and 148 and 152 aren't known;
  - type 14 is taken to be a jagged line;
  - types 4 and 10 weren't seen in play, and are described from the
    renderer only;
  - 20 bytes past Quake III's 140 were always 0;
  - the shader time is taken to be at Quake III's 128;
  - mark polygons' vertices are taken to be Quake III's 24-byte layout.

  See [eliteforce-syscalls.md](formats/eliteforce-syscalls.md).
- **Single player's scene entities** have 12 bytes of Elite Force's own
  (128-144) whose meaning isn't described. See
  [eliteforce-sp-interface.md](formats/eliteforce-sp-interface.md).

## Single player

- **Names.** The functions are named after Jedi Outcast's GPL source, by
  their places and arguments. Elite Force's own names aren't known.
- **Partly understood calls:**
  - the client game's third force-feedback call (36);
  - the menus' export 5 `(0, &out)` and exports 8 and 9 `(server, info)`,
    whose arguments aren't named;
  - renderer entry 28, an A3D sound cache call that nothing calls;
  - Init's 5th argument, "a time taken at start-up".
- **The player state** (544 bytes): only a few fields are known (origin,
  view height, stats, ammo, Borg adaptation).
- **The `STRP` save chunk**'s meaning: "an engine table of the client's,
  n×n ints".
- **The Expansion's optional save read** (its import 19): no caller was
  found in its game code. Reluva serves it as the engine does anyway.
- **Bink videos** were checked against FFmpeg on public samples. Elite
  Force's own videos play, but weren't compared frame by frame.

## Holomatch and network play

The protocol was written from Lilium Voyager (a GPL engine for Elite Force)
and ioquake3, then played against the retail game patched to 1.2. What
that play didn't exercise is still open. See
[eliteforce-network.md](formats/eliteforce-network.md) ("Open
questions").

- **Older versions:** 1.0's and 1.1's protocol, and whether a 1.2 client can
  talk to them.
- **1.2's player state** is 8 bytes longer than 1.0's: `introTime` at 56,
  and one more word whose place isn't pinned down. The fields Reluva reads
  are at known offsets in both layouts.
- **Server lists:**
  - which info keys the retail menus require;
  - how many ports the retail browser scans, and whether it filters on
    `gamename`;
  - how Raven's master server ended its list.
- **Connecting:**
  - what a retail client sends with `getchallenge`;
  - whether it checks the server's `connectResponse`.
- **Bit stream:** whether the retail game skips an extra byte before raw
  data. It matters only after a download.
- **Other details:**
  - whether anything sends the protocol's "nop";
  - the retail defaults for `rate` and `snaps`;
  - user command button 128 (unused as far as seen);
  - several of Lilium's behaviours taken to be the original's: fragmented
    messages, `gamename`, `nettype` as text, and querying Raven's master
    with `getstatus`.

## Holomatch's game code

- **Calls numbered by Quake III's order.** Many system calls weren't seen
  with telling arguments. They're taken to be Quake III's call at the same
  number, which the calls that were seen fit:
  - the menus' calls 2-3, 6-7, 9-12, 15, 18-19, 21-30, 33-42, 44-45 and
    55-56;
  - the game's calls 2, 4, 8-9, 11-14, 16-17, 20-22, 25-35 and 111;
  - the client game's calls 14, 20, 22, 24, 26, 38, 42-43 and 53;
  - the menus' `vmMain` commands 2-9.

  See [eliteforce-syscalls.md](formats/eliteforce-syscalls.md).
- **Structures taken to be Quake III's:**
  - `uiClientState_t`;
  - the key numbers;
  - the game entity's words at 208 and 212 (two of `linkcount`, `svFlags`
    and `singleClient`);
  - the `glconfig_t` word at 5120 (left 0).
- **Player state word 52** stayed 0 in every run; its meaning is unknown.
- **The Holomatch intro:** the client game presumably ends it
  (`g_holoIntro`).
- **The sound calls** (client game 28-35) are taken to be Quake III's by
  what they're passed.
- **The shared calls 100 and up** (memset, sin and so on) are the same in
  every module as far as the logs show.

## Bots

- **The bot library** is taken to be Quake III's, with its file formats and
  rules read from ioquake3. That covers the AAS format, the characters,
  the weights, chat and movement.
- **Elementary actions:** say (400) has no evidence. Several others are
  unused by the game, so their numbers come from Quake III's order.
- **Routing** gives the same kind of routes as the original's, not proven
  identical.

## Input

- **Joystick:**
  - the meaning of `in_joyBallScale` isn't known;
  - the hat's key numbers (204-207) are never set;
  - SDL is assumed to report a device's buttons and axes in the same order
    Windows' joystick API did.

  See [eliteforce-input.md](formats/eliteforce-input.md).

## Map entities

Keys and spawn flags whose meaning comes only from their names, from Jedi
Outcast by analogy, or isn't known. None matters to the engine; the game's
own code reads them. See
[eliteforce-entities.md](formats/eliteforce-entities.md) ("Still
unknown").

- **Not known:**
  - `light`'s `radius` and spawn flag 32;
  - `func_door`'s `dmg -1` (probably no damage when blocked);
  - `target_speaker`'s `soundgroup`;
  - `worldspawn`'s `infostring` (`II_<MAP>`, probably the objectives or
    loading text) and its editor leftovers;
  - `target_scriptrunner`'s spawn flags;
  - `cameragroup`;
  - `npc_stasis`'s `ownername`, `closetarget` and `target2`;
  - NPC spawn flags;
  - `hiddenwaypoint`;
  - `fx_crew_beam_in`'s `type`;
  - breakables' `fullname`;
  - single player's spawn flags in general.
- **From the name only:**
  - `light`'s `scale` and `color`;
  - `misc_model`'s `autobound`;
  - `worldspawn`'s `gravity`;
  - `target_change_parm`;
  - `noscavsounds`;
  - `leaddist`;
  - the fx colour keys;
  - breakables' `material` and splash damage;
  - `func_stasis_door`;
  - the `target_*` and `trigger_*` classes.
- **From Jedi Outcast:**
  - script hooks and parms;
  - `opentarget` and `closetarget`;
  - sound sets;
  - NPC keys;
  - navigation goals' sizes;
  - `point_combat`;
  - `func_usable`.
