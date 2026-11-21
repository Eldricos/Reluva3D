# Jedi Outcast: the multiplayer network protocol (15, 16)

> **Status:** done · **Code:** `src/engines/idtech3/wire/huffman.cpp`, `wire/message.cpp`, `wire/netchan.cpp`, `src/engines/idtech3/jk2/net_fields.cpp`, `eliteforce/net_protocol.cpp`, `net_server.cpp`, `net_client.cpp` · **Checked against:** jk2mp.exe 1.02a's own tables and Huffman code, run in Reluva's x86 interpreter (November 2026); Reluva to Reluva, and the retail game both ways · **Confidence:** the tables and the Huffman code confirmed; the rest from the code unless marked

## Overview

Jedi Outcast 1.02a's multiplayer speaks protocol 15. It is Quake III
1.32's protocol (Huffman-coded messages, keyed XOR of every message,
field-by-field deltas with a change count) carrying Raven's larger
entity and player states, with one opcode of its own. Reluva runs it
with the server and client it uses for Elite Force's protocol 24
([../../stvef/systems/network.md](../../stvef/systems/network.md)),
told the differences by a protocol description (`NetProtocol`).

This page gives what differs from Elite Force's page, which describes
the parts they share: the transport, the connection sequence, server
commands, configstrings, pure servers and downloads.

## How it works

### Constants

| Name | Value | Where from |
|---|---|---|
| Protocol | 15 | the `protocol` the server's info carries; the menus' search asks for it |
| Server UDP port | 28070, the local search trying the three after it too | jk2mp.exe (`push 0x6da6` at 0x40acb6; 0x40d892 adds the port's number) |
| Master server port | 28060 | jk2mp.exe (0x40d975) |
| Configstrings | 1,400 | the client game ([cgame-calls.md](cgame-calls.md)) |
| Entity state | 296 bytes | jk2mp.exe clears a `0x128` byte state for baselines (0x449516) |
| Player state | 1,368 bytes | both modules ([game-calls.md](game-calls.md)) |
| Entity number bits | 10 | as Elite Force's: 1023 ends a list |

### Out of band

As Elite Force's ([its page](../../stvef/systems/network.md#transport)),
except:

- `infoResponse` is followed by a newline and the info string, not
  quoted (Quake III's way). The server's keys: `challenge`, `protocol`,
  `hostname`, `mapname`, `clients`, `sv_maxclients`, `gametype`, `pure`,
  `sv_allowAnonymous`.
- The `connect` packet is compressed after its first 12 bytes (the four
  `0xff` and `connect `): the rest (the quoted userinfo) is an adaptive
  Huffman code, below, with its length in two bytes, high first.
- **Heartbeats**, Quake III 1.32's (jk2mp.exe, 0x44e460): only a public
  server (`dedicated 2`) sends them, an out-of-band `heartbeat
  QuakeArena-1` and a newline to each of `sv_master1` to `sv_master5`
  (the first `masterjk2.ravensoft.com`, Raven's, long gone; the others
  empty), port 28060 where a name gives none. One goes every 5 minutes,
  one at once when a level starts (0x44dcc4 runs the `heartbeat`
  command), and two as the server shuts down, so the masters ask, get no
  answer and drop it. A master's name is looked up again when its cvar
  changes (below). Not, as Elite Force's, when the server empties, gets
  its first player or fills.

### The netchan

Elite Force's header (sequence, the client's qport, fragments of 1,300
bytes), but nothing is scrambled. Instead each message is XORed, after
the netchan has taken it, as Quake III 1.32's is:

- **Server to client**: from the message's 5th byte on. The key starts as
  the low byte of the client's challenge XOR the message's sequence; the
  text is the last command the client sent that the server ran.
- **Client to server**: from the 13th byte on. The key starts as the low
  byte of the challenge XOR the level's id (`sv_serverid`) XOR the last
  server message received; the text is the last server command received
  (the one acknowledged in the message), from a ring of 64 by sequence.
- For each byte, the key is XORed with the text's next character (from its
  start again at its end; an empty text gives 0), shifted left one bit
  where the byte's place is odd; `%` and characters above 127 count as
  `.`. The byte is XORed with the key's low 8 bits.
- The receiver reads the words that key the message (the server message's
  first, the client message's first three) from the message as it came,
  then undoes the XOR from the same place. If those words' code ran past
  the XOR's start, both ends would disagree, as in the original; with the
  numbers sent in practice it doesn't happen.

### Bit stream

Messages after the netchan header are Huffman-coded:

- A value's bits past whole bytes (`bits & 7`, lowest first) are written as
  they are; then each of its bytes, lowest first, as its code. Bits fill
  each byte from its lowest up.
- Strings and raw data are bytes like any other: nothing is aligned. A
  writer turns string bytes above 127 into `.`; a reader turns `%` and
  bytes above 127 into `.`.
- A message ends with an end marker: 9 from the server (after
  `svc_mapchange`, 8), 5 from the client.
- **A message's size** is a byte more than its last bit's (the writer,
  0x431010, sets it to bit / 8 + 1 after every write), so one ending on a
  byte boundary carries a byte of nothing. It matters: the readers take a
  read that ends at the size as past the end (a long comes back as −1),
  and the client drops a server message read past its end.
- Signed fields (a negative bit count in the tables) are read back
  extended from their top bit.
- A user command's 16-bit fields (angles, buttons) are read back with the
  key's bits above 16 left in them, at both ends.

#### The Huffman code

Quake III's adaptive Huffman code (the FGK scheme, as Sayood describes
it): the tree starts as one leaf, "not yet transmitted"; a new byte splits
it into an inner node holding it (left) and the byte's leaf (right), both
ranked just above it; each time a byte is counted, its leaf and then each
node up to the root is moved to the top of the nodes of its weight
(swapped with the highest-ranked one, in the tree unless that one is its
parent, and always in the ranking) and its weight goes up; a node ranked
just above its parent after that is swapped with it in the ranking. Two
quirks shape the tree and must be kept: swapping two children of one
parent leaves them where they were (the parents' pointers are changed one
after the other), and the parent swap above changes only the ranking.

A code is the path from the root to the byte's leaf, 0 left and 1 right,
root first.

- **Messages** use one tree, built once by counting each byte as many
  times as jk2mp.exe's table says (256 counts at 0x4f6310, 1,053,340 in
  all; the same as Team Arena's), and never changed after. The longest
  code is 11 bits.
- **The connect packet** is coded from an empty tree, counting each byte
  after it's sent: a byte not seen before is sent as the code of "not yet
  transmitted" and its 8 bits, highest first. The packet's length is
  rounded up to a whole byte and one more.

### Entity state

296 bytes. Fields are sent in jk2mp.exe's order (its table at 0x4f5ab0:
name, offset, bits for each of 73), which starts with the position and
its trajectory and ends with the saber and force fields. An entity delta:

1. The entity number, 10 bits; 1 bit: removed (nothing follows).
2. 1 bit: changed; if 0, nothing follows.
3. A byte: how many fields to read, the last that changed + 1.
4. For each of those: 1 bit, changed. A changed float: 1 bit, 0 for zero;
   else 1 bit, 0 and the whole number + 4096 in 13 bits (−4096 to 4095),
   or 1 and its 32 bits. A changed integer: 1 bit, 0 for zero; else its
   value in the field's width (signed fields, such as `modelindex` at −8,
   sign-extended when read).
5. Fields past the count are kept from the old state.

### Player state

1,368 bytes; 105 fields in jk2mp.exe's order (0x4f5e20), then four arrays.

1. A byte: how many fields, as for entities.
2. For each: 1 bit, changed; a float as above but with no zero bit; an
   integer in its width, with no zero bit.
3. 1 bit: any array changed. Then for stats (216), persistant (280), ammo
   (408), each 16 elements of 16 bits, and powerups (344), 16 of 32 bits:
   1 bit, changed; if so a 16-bit mask of the elements, then each one
   changed. (The arrays' places are the GPL `playerState_t`'s, which puts
   `generic1` at 472 as jk2mp.exe's table does.)

### User commands

28 bytes in the game ([game-calls.md](game-calls.md)). Each is sent as a
change from the one before it in the packet:

1. 1 bit: 1 and the time's step in 8 bits if under 256 ms, else 0 and the
   time in 32.
2. 1 bit: 0 if nothing else changed.
3. Each field (the three angles, 16 bits; forward, right, up, 8; buttons,
   16; weapon, force power, item, generic command, 8): 1 bit changed, then
   its value XOR the key, in its width.

The key is the level's checksum feed XOR the last server message received
XOR a hash of the last server command received (its first 32 characters:
the sum of each, signed, times 119 plus its place; then XOR that shifted
right 10 and 20), then XOR the command's time. Reading, the original masks
the key one bit wider than the field (its table of masks is off by one),
so a 16-bit field can come back with bit 16 set; Reluva does the same.

### Messages

As Quake III 1.32's:

- **Server messages** start with a long, the last client command the
  server ran (Elite Force carries it in its snapshot instead), then the
  server commands the client hasn't acknowledged, then the rest, then 9.
  The snapshot has no acknowledgement of its own.
- **The game state** ends its list of configstrings and baselines with 9,
  then the player's client number and the checksum feed (Elite Force's
  ends with 0 and has no client number). In jk2mp.exe: 0x449568.
- **`svc_mapchange` (8)**: the server's notice that it's about to load
  another level, alone in a message after the commands. jk2mp.exe sends
  it (0x4495c0) to every player at least connected and not a bot
  (0x44d7d0), five times as it spawns the new level (calls from 0x44d83e
  to 0x44d9ed), at its steps before the new map loads, in case some are
  lost. The client
  hands it to its client game (export 24, `CG_MAP_CHANGE`), as often as it
  comes; 1.02a's `cgame.qvm` sets a flag (vmMain's case at instruction
  271) that has it show "Server changing maps" (`INGAMETEXT`
  `SERVER_CHANGING_MAPS`) until the new level's game state starts it
  again.
- **A refused download** is block 0 with the size −1, then a string saying
  why.
- **Client messages** are Elite Force's (the level's id, the message and
  command acknowledged, commands, then moves) and end with 5.

### Patches

1.03 changes the wire without changing the protocol's number; 1.04
changes the number to 16 and nothing else on the wire (`jk2mp.exe`
1.02a, 1.03 and 1.04 compared function by function):

- **The player state** is 12 bytes longer (1,380): three new one-bit
  fields after `isJediMaster` (604), at 608, 612 and 616
  (`forceRestricted`, `trueJedi`, `trueNonJedi` by the table's names),
  and every field from 608 on moved 12 bytes along: 108 fields (the table
  at 0x4fd1b8 in both). The arrays stay where they were.
- **The entity state** keeps its size; `modelGhoul2` (field 44) is sent in
  5 bits, not 4 (the table at 0x4fce48).
- **The protocol** stays 15 in 1.03, so a 1.02a player and a 1.03 server
  pass each other's check and then misread every player state; nothing
  in the handshake tells them apart *(inferred)*. 1.04's is
  16: the connect, the server's check and answer, the info and the
  `protocol` setting (from 0x40ba09 to 0x44f0cb), and its recordings end
  `.dm_16`.
- **The server's info** (1.03's SVC_Info, 0x44f160) has no `pure`; after
  `gametype` it gives `needpass` (`g_needpass`), `truejedi`
  (`g_jediVmerc`), `wdisable` (`g_duelWeaponDisable` in a duel, game type
  3, else `g_weaponDisable`) and `fdisable` (`g_forcePowerDisable`).
- Unchanged: the Huffman code, the netchan (1.04 only checks fragments'
  lengths harder), user commands, the messages' order and the ports.

Reluva takes the release from the game's paks: `netProtocol()` gives
1.03's and 1.04's tables and info, and 1.04's number.

## In Reluva

`NetProtocol` (`eliteforce/net_protocol.cpp`) names what differs between
protocols 24 and 15; the server and client follow it, and `netProtocol()`
picks a game's. The Play page hosts its matches with the game's protocol
and port, and Join Server connects with them; `reluva-tool idtech3
net-run` hosts or joins either game's servers, or (`-loopback`) runs a
player joining a server in memory.

The Play page's server sends the map change at the five steps of its
own level change that match the original's (`NetServer::levelChanging`):
first, once the client game and sounds are shut, once the renderer is
cleared, before the new map loads and once it has; its client hands
each one to the client game.

A public server (`dedicated 2`: `net-run -public`) sends Quake III's
heartbeats to Jedi Outcast's masters; Reluva's defaults after Raven's are
the community's `master.ouned.de` and `master.jkhub.org`. As the
original's (0x44e460), each master's name is looked up again at the
heartbeat after its cvar changes ("Resolving", then "resolved to" or
"Couldn't resolve address"), one that can't be is emptied so it isn't
tried again, and each is sent "Sending heartbeat to". The original's
check for a port in the name is `strstr(":", name)`, the wrong way
round, so a master is always at port 28060, a port in the name or not;
Reluva's is too.

The CD key's authorization is left out: Raven's authorize server has
long been gone, so there's nothing to ask, and every server has let
everyone in since ([../KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

## Sources and checks

- **Where the facts come from:** jk2mp.exe 1.02a: its entity and player
  state tables (each field's name, offset and width), its Huffman table,
  its ports, and the opcodes it writes the game state with (found with
  `objdump`); the GPL Jedi Outcast multiplayer source (`msg.cpp`,
  `huffman.cpp`, `net_chan.cpp`, `cl_net_chan.cpp`, `sv_net_chan.cpp`,
  `sv_client.cpp`, `sv_snapshot.cpp`, `sv_main.cpp`, `cl_main.cpp`,
  `cl_input.cpp`, `qcommon.h`) for the rest, read as documentation; the
  code is new.
- **How it was checked:**
  - `reluva-tool idtech3 huffman-check <jk2mp.exe>` runs jk2mp.exe's own
    routine that builds the message tree (0x4326f0) in the x86
    interpreter and compares every byte's code with Reluva's: all 256 the
    same. It then compresses 64 connect texts with jk2mp.exe's own
    compressor (0x430450) and with Reluva's: all the same, and all come
    back. (By hand: the imports Reluva makes don't keep jk2mp.exe.)
  - A unit test (`idtech3.unit.network`, "jedi outcast wire"): the code,
    the connect compression, messages, entity and player deltas, keyed
    commands and the XOR, each back as it went.
  - `net-run -loopback` on ffa_bespin, recorded by the game-files step
    `jk2-game`: a player joins in memory, gets the game state (player 0)
    and 117 snapshots, joins the game and runs 527 units forward. With
    `-mapchange ffa_deathstar` (recorded too), the server changes levels
    halfway: the player gets the notice, then the new level's game state
    the frame after, and plays on.
  - Live, November 2026: the Play page's match found and joined over the
    system's sockets by `net-run -join`, and a `net-run` server found by
    Join Server's local list (on 28070), joined from it, and played.
  - `reluva-tool idtech3 delta-check <jk2mp.exe> [pairs]` runs
    jk2mp.exe's own entity and player-state delta writers and readers
    (0x431aa0, 0x431cd0, 0x431f60, 0x432340), its keyed user-command
    writer and reader (0x431750, 0x431900) and its client's XOR
    (0x40e0c0) in the x86 interpreter beside Reluva's, on made-up states
    that use every field of its tables (73 an entity's, 105 a player's,
    and the player's four arrays): 2,000 of each, November 2026, all
    written to the same bits and size and read back the same either way.
    It found Reluva's messages a byte short when their bits ended on a
    byte boundary (above), which the retail game would have read as past
    their end.
    The same on 1.03's and 1.04's `jk2mp.exe` (their routines from
    0x432420 and 0x432440, the client's XOR left out), November 2026:
    2,000 of each, written and read back the same, with their 108 player
    fields.
  - A unit test (`idtech3.unit.network`, "jedi outcast heartbeat"): the
    heartbeat's text and port, none again within 5 minutes, one at a new
    level and two at shutdown; a master changed looked up again, its port
    passed over, one that can't be found emptied. Not seen reaching a real master: in
    November 2026 none of the community masters' names
    (`master.ouned.de`, `master.jkhub.org`) could be looked up from where
    Reluva was tested.
  - Against the retail game on Windows, by hand, November 2026: the
    retail client joined a match Reluva hosted, and Reluva's client a
    retail server, and both played (players drawn facing the right way
    once Ghoul2's root turn was in, [ghoul2.md](../../../formats/ghoul2.md)).

### Open questions

- A level change, downloads and long matches haven't been tried against
  the retail game yet, nor a Reluva server's heartbeats against a real
  master.
