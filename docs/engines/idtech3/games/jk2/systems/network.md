# Jedi Outcast: the multiplayer network protocol (15)

> **Status:** done · **Code:** `src/engines/idtech3/wire/huffman.cpp`, `wire/message.cpp`, `wire/netchan.cpp`, `src/engines/idtech3/jk2/net_fields.cpp`, `eliteforce/net_protocol.cpp`, `net_server.cpp`, `net_client.cpp` · **Checked against:** jk2mp.exe 1.02a's own tables and Huffman code, run in Reluva's x86 interpreter (November 2026); Reluva to Reluva · **Confidence:** the tables and the Huffman code confirmed; the rest from the code unless marked

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
- A server that sends heartbeats sends `heartbeat QuakeArena-1` (not yet
  in Reluva: its server doesn't announce itself to masters).

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
- **`svc_mapchange` (8)**: sent before the server loads another level; the
  client tells its client game (`CG_MAP_CHANGE`). Reluva's client takes it
  and does nothing yet.
- **A refused download** is block 0 with the size −1, then a string saying
  why.
- **Client messages** are Elite Force's (the level's id, the message and
  command acknowledged, commands, then moves) and end with 5.

## In Reluva

`NetProtocol` (`eliteforce/net_protocol.cpp`) names what differs between
protocols 24 and 15; the server and client follow it, and `netProtocol()`
picks a game's. The Play page hosts its matches with the game's protocol
and port, and Join Server connects with them; `reluva-tool idtech3
net-run` hosts or joins either game's servers, or (`-loopback`) runs a
player joining a server in memory.

Not yet: heartbeats to Jedi Outcast's masters; `svc_mapchange` handed to
the client game; the CD key's authorization (Reluva lets everyone in, as
every server has since Raven's authorize server went).

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
    and 117 snapshots, joins the game and runs 527 units forward.
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
  - Not yet played against the retail game, either way: the handshake,
    the game state and snapshots as whole messages, and the netchan's
    headers and fragments are checked only between Reluva's ends.

### Open questions

- Whether the retail game takes Reluva's server and client as they are:
  every table, the Huffman code, the deltas, the commands and the
  client's XOR agree with its own routines, but nothing has been played
  against it yet.
