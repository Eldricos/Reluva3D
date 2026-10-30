# Need for Speed III: network messages

What two copies of `nfs3.exe` say to each other in multiplayer, over TCP
and over IPX. Worked out in October 2026 from traffic between two copies
in `nfs-host` (`-netlog`, which writes each message sent and received)
and from the game's own code, read in its disassembly. Integers are
little-endian unless marked BE (big-endian).

## How it was worked out

- **Captures.** Two copies on one machine (`-net -netaddr 127.0.0.2` and
  `127.0.0.3`): one hosts, the other joins, a chat line is typed, both
  ready up and race Hometown for about a minute. Once over TCP, once over
  IPX (carried over UDP, [nfs.md](../../engines/nfs.md)).
- **Code.** The Winsock calls go through thunks (send `0x533e26`, recv
  `0x533e2c`, sendto `0x533e32`, recvfrom `0x533e5c`). From their callers
  to EA's network library (its source names: `cmn\connect.c`,
  `cmn\layer.c`, `cmn\netgathr.c`, `win\tcp.c`, `win\ipx.c`), and from
  that to the game's dispatcher.
- **Checks.** The IPX checksum was recomputed over all 9,869 captured
  data packets, every one matching. The dispatcher's table was read from
  the file. Field meanings marked *unknown* below are copied whole by the
  game into structures whose use wasn't followed.

## Layers

| Layer | What it is |
|---|---|
| Game | a type byte, then the payload (Messages, below) |
| TCP | a BE u16 length (type byte and payload), then the message |
| IPX | a BE tag (`0xEA` and three letters); game messages go in `ea dat` packets, with a reliable layer's 6-byte header |

**TCP.** Sending is at `0x4f7030`, receiving at `0x4f6640`: it peeks at 2
bytes, then at length + 2, hands the message on (`0x5126f0`), then reads
it. A length over the connection's limit is fatal; the receive buffer is
1 KB. `recv` giving 0, or any error but WSAEWOULDBLOCK, ends the link.

**Topology.** A star: the host relays player records (03) and chat (0a)
and sends the combined race frames (06), in both captures.

## Messages

The dispatcher (`0x4a6440`) calls the handler for types below 0x13 from
the table at `0x55ecbc`, given the connection, the message and its
length. Messages are dropped while the session is being abandoned (word
`0x7a22dc`). Sending (`0x4a60e0`, to one peer or all) retries 9 times,
sleeping 10 + 50 × i ms, before giving up on the player.

| Type | Bytes (with the type) | Contents | Handler |
|---|---|---|---|
| `00` | 41 | 40 bytes kept at `0x55ed08`: the version, `27, 2.0`, NUL-ended, then whatever the buffer held | `0x4a6df0` |
| `01` | 124 | the session, kept at `0x7a2214`: +1 the host's name; +0x62 u16 players (2); +0x64 u16 the receiver's slot (1); from +0x6a, 0xFF for each empty slot | `0x4a68d0` |
| `02` | 40 | the race settings, kept at `0x79f8f0` (below) | `0x49b2b0` |
| `03` | 96 | a player, kept at `0x79f5f0` + slot × 0x60: +4 u32 slot; +8 u8 ready (0, then 1); +9 name (32); +0x29 make (`Chevrolet`, 16); +0x39 model (`Corvette`, 16); the rest unknown. A copy ignores its own; the host passes others' on | `0x49b310` |
| `04` | 4 | a barrier: +2 u16 its number (1, 0x0f, 0x10, 0x17, 0x18, 0x1f, 0x0d, 3-6 seen; 0x0c abandons). The host sends, each player answers with the same, within a timeout | `0x4a6c40` |
| `05` | up to 209 | a piece of a block, appended (the race's set-up: 2,192 bytes in 11 pieces; below) | `0x4c6080` |
| `06` | 33, host to all | one race frame: 8 u32 input words, one for each car | `0x4c61e0` |
| `06` | 6, player to host | +1 s8 how many frames ahead (0-3); +2 u32 its input word (sent by `0x4c60c0`) | `0x4c61e0` |
| `0a` | any | chat: NUL-ended text (`Player1: hi`); the host passes it on | `0x42d710` |
| `07` | 20 | link set-up over serial and modem (`0x4c4310`): +4 u32 the frame count `0x79c1a4`; +8 the player's name (`Player1`). Stored at `0x7d3650` (Serial, below) | `0x4c4190` |
| `08` | 12 | the pause menu: +4 u32 an operation, +8 u32 a frame (below) | `0x4c5b00` |
| `09` | 16 | +1 the sender's car; +4, +8, +0xC three u32 flags for its steering, gas and brake bindings (`0x43c000`), kept at `0x6fd550` + car × 0x6C | `0x43dab0` |
| `0b` | 14 + 2 per AI car | a status every 32 steps (`0x4c7bd0`, [nfs-threads.md](../../engines/nfs-threads.md)) | `0x4c7d30` |
| `0c` | 196 | +1 a car; +4 0xC0 bytes of the car's state (car + 0x224), copied in. Sent for every car between barriers 0x22 and 0x23 (`0x4c7ea0`) | `0x4c7f90` |
| `0d` | 2 | +1 a car that has finished: its +0x2C4 is set to 3 (`0x4c7e50`) | `0x4c7e80` |
| `0e` | 18 | +2 eight u16, one per slot, to `0x7a22ca`: 2 for a player the host has dropped (`0x49ecf0`) | `0x49edb0` |
| `0f`, `11` | 28 | a car offered: +1 its 16-byte name, +0x14 u32, +0x18 u32 its size. If the car isn't installed (`0x439990`), a `car.viv` is opened to write (`0x49b540`). `0f` is read by the host, `11` by the others | `0x49b3e0`, `0x49b470` |
| `10`, `12` | any | the next piece of that car's file (the bytes after the type) | `0x49b500`, `0x49b520` |

Sizes for 07-12 are what the senders pass to `0x4a60e0`; 0f-12 weren't
seen sent, so their sizes are from the handlers. Only 07 is in the
captures (serial and modem); the others belong to pausing, quitting,
the status checks and downloading a car, which the captured runs
didn't do.

**The race settings** (02). The host builds the message from the
front end's variables (`0x49aa80`) and sends it whenever they differ
from the last one sent (`0x49a65e`); the others copy it back into the
same variables (`0x49a800`). The front end's option table at `0x5563fc`
(20-byte rows: name, text lists, variable) gives the names and values:

| Offset | Size | Variable | Option (values) | Captured |
|---|---|---|---|---|
| 0 | 1 | | the type, 2 | 2 |
| 1 | 1 | `0x6fd3b8` | `racetype`: Single Race, Tournament, Knockout, Hot Pursuit (0-3) | 0 |
| 2 | 1 | `0x6fd4ac` | `tracks`: the track (0 Hometown ... 8, [tracks.md](tracks.md)) | 0 |
| 3 | 1 | `0x6fd504` | `classLimiter`: Limit to A, B, C, Unlimited (0-3) | 3 |
| 4 | 1 | `0x6fd4b4` | the number of laps (2, 4 or 8, from +5 by `0x4cf190`) | 2 |
| 5 | 1 | `0x6fd4b0` | `laps`: 2, 4, 8 (0-2) | 0 |
| 6 | 1 | `0x6fd4d4` | `opcars`: the opponents' car; 4 and up is a car (its number + 4) | 2 |
| 7 | 1 | | not set | 0 |
| 8 | 16 | | when +6 is 4 or more, that car's name (car record +0x31C, `0x439520`); the receiver finds its own number for it (`0x439990`) | 0 |
| 0x18 | 1 | `0x6fd4d0` | `oppnumber`: None, One, Full Grid (0-2) | 2 |
| 0x19 | 1 | `0x6fd4e8` | `traffic`: On, Off (0, 1) | 0 |
| 0x1A | 1 | `0x6fd4c4` | `mirrored` (0, 1) | 0 |
| 0x1B | 1 | `0x6fd4cc` | `weather` (0, 1) | 0 |
| 0x1C | 1 | `0x6fd4c8` | `night` (0, 1) | 0 |
| 0x1D | 1 | `0x6fd4c0` | `direction`: Forward, Backward (0, 1) | 0 |
| 0x1E | 2 | | not set | 0 |
| 0x20 | 4 | `0x6fd3b4` | the random seed: `GetTickCount` at start-up (`0x47242c`), given to `0x4371d0` and `0x4e0dee` | 0x18B5-0x1959 |
| 0x24 | 1 | `0x6fd470` | `gamelevel`: Beginner, Expert (0, 1) | 0 |
| 0x25 | 3 | | not set | 0 |

The "Captured" column is the same in every run that raced (TCP, IPX,
serial and modem) but for the seed. Tournament and Knockout set Full Grid,
traffic off and their own laps; Hot Pursuit sets Full Grid (`0x44bedd`,
`0x44c095`, `0x44c23b`). What `opcars` 0-3 mean wasn't followed.

**Pausing** (08, `0x4c55a0`, `0x4c5720`). The u32 at +4:

| +4 | Sent by | Meaning |
|---|---|---|
| 0 | a player who pauses, to the host; the host answers all | pause. The host picks the frame (frames stored + 2, `0x4c6970`) and sends it at +8; when each copy reaches it, it stops sending controls (`0x560108`) and waits at barrier 0x1E |
| 1 | a player leaving the pause menu, to all (`0x4c5a68`) | continue: the host notes it (`0x7d36c4`) and ends the pause with barrier 9 |
| 2 | no sender found | the others take state 0xA, which ends the pause with barrier 0xA (race again, probably) |
| 3 | a player quitting from the pause menu, to all (`0x4c5a90`) | quit: the host marks that player's slot 0xB and drops it (`0x49ecf0`, which sends 0e); the others end the race (state 0xB) |

The menu's five outcomes end in barrier 9, 0xA, 0x20, 0xB or 0xB
(`0x4c5883`-`0x4c5990`, table `0x4c5704`); the fifth also abandons the
session (`0x7a22dc`). Pauses are at least 32
frames apart (`0x7d36cc` + 0x20).

**The race set-up** (05): a header of 0x1A0 bytes, which repeats the
settings of 02, then 8 cars of 0x6C bytes (+0x20 u32 the driver's
number, +0x24 the name, `Ram Rod`, `Lead Foot`..., six floats of 1.0, the
car's codes), then the starting order, 8 u32.

**The race** is lockstep: in the captured races no positions or speeds
went over the network, only each car's controls (0b and 0c, which carry
some, are sent only with `0x6fd50c` set). Each car keeps the last 128 frames' words (at
`0x7d3714` + car × 0x200). A player sends its word to the host each
frame; the host sends every car's word for the frame. For a player
that's late, the host repeats its last word with byte 3 masked by 0x8D.

**The input word** is the 4 bytes built at `0x48f550` into `0x79c338`
(a second player on the same machine at `0x79c33c`) and sent by
`0x48fa10`. The game reads it back at `0x431430` through `0x48f6a0`:

| Byte | Bit | Control | Default key | Read as |
|---|---|---|---|---|
| 0 | | steering, s8: (right − left) / 2; swapped on a mirrored track (`0x48f215`) | keypad 4/6 | car +0x518 |
| 1 | | gas, 0-255 | keypad 8 | car +0x504 |
| 2 | | brake, 0-255 | keypad 2 | car +0x505 |
| 3 | 0x01 | never bound (slot 4 is set by nothing) | | |
| 3 | 0x02 | reset the car (`0x429100`; a cop's `0x405220`) | R | |
| 3 | 0x04 | handbrake | Space | car +0x50D |
| 3 | 0x08 | horn (siren): a sound held while set (`0x4120ec`) | H | car +0x50E |
| 3 | 0x10 | shift up (`0x48f6e0`, `0x48f710`) | A | |
| 3 | 0x20 | shift down | Z | |
| 3 | 0x40 | lights: steps car +0x660 through 0, 1, 2 | L | car +0x660 |
| 3 | 0x80 | an action with no name in the text table; `0x404e40` plays sounds 0x10-0x12 and starts a timed event | S | car +0x512 |

How found: `0x48f940(slot, binding)` fills a table of 4-byte bindings at
`0x79c340`; `0x48f1f0` copies the 13 bindings of the player's controls
(`0x6fd20c`) into it, so `+0x10` goes to slot 6 (bit 0x04), `+0x14` to
slot 8 (0x10) and so on. The keys are the scan codes in
`FeData/config/config.dat` at 0x16CC (one per binding, in the same
order). Bits 0x02 and 0x10-0x80 act once per press (the mask 0xF2 at
`0x48f6be`); 0x04 and 0x08 while held. The host's 0x8D keeps the held
ones and 0x80. Camera view (C) and look behind (B) are not in the word:
they go to bits 22 and 21 of a separate 32-bit word of the machine's own
keys (built at `0x48f645`, ring `0x79c2b8`, never sent), with pause,
map and the other keys set at `0x48f970`.

In every captured race all the words were 0: the controls were never
touched, so the table was read from the code only.

## IPX

IPX datagrams start with a BE tag, `0xEA` and three letters, and carry at
most 280 bytes after it (`0x4f60b0`); the socket (`socket(6, 2, 1000)` at
`0x4f5cb2`) receives into 284.

**Finding games** (`netgathr`, IPX socket 0x452):

- A question: BE u16 1, BE u16 0x08BE, broadcast (`0x5127d0`).
- An answer: BE u16 2, then entries of 64 bytes (`0x512c80`, `0x512940`):
  BE u16 0x08BE; 48 characters, `EAGAME.` and a GUID in hex; the game's
  IPX address (network 4, node 6, BE socket 2); BE u16 1 while the game is
  open (0x10 when it's withdrawn). Sent to the asker and broadcast, and
  broadcast again every 60 s.

**Joining** goes by tags (`0x5133a0`): `srv` (the game's name, from the
player), `dec` (176 bytes, the game: its GUID, address and name), `add`
(140, the player asking to join), `rsp` (36), then `opn` and `dat`. The
host sends `ola` every 100 ticks (781 ms); a player not heard from in 450
ticks (3.5 s) is dropped. Others the code knows: `ply`, `req`, `no!`,
`bye`. Their fields were read from the captures, not the code.

**Data** (`dat`, `0x51c7c0`-`0x51da60`): after the tag, a header, then
the game message:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | 0xFE (the receiver resynchronises on it) |
| 1 | 1 | payload length (at most 0xFA) |
| 2 | 1 | sequence number (7 bits) |
| 3 | 1 | the last sequence received in order (over 0x7F: none) |
| 4 | 2 | BE checksum: CRC-16/ARC (the reflected polynomial 0xA001, table at `0x566b54`) from 0xFBEA, over header and payload with these two bytes 0 (`0x4f4780`) |

A packet of length 0 is a control: sequence 0xFB acknowledges (and keeps
the link alive), 0xFF ends it, 0x7F or below asks for that packet again
(`0x51cc00`). Packets out of order wait and are delivered in order. Each
good packet resets a counter of 1,920 ticks (15 s, set at `0x4a6506`);
at 0 the link is closed (`0x51d8f0`). The library's clock runs at 128
ticks a second.

**Timers and window** (`cmn\packet.c`). Each transport sets three u16
when it opens a link, as fractions of `0x5643d8` (128):

| Transport | +0x2A acknowledge | +0x2C ask again | +0x2E resend | Set at |
|---|---|---|---|---|
| IPX | ÷ 30 = 4 | ÷ 15 = 8 | ÷ 15 = 8 | `0x514ec0` |
| modem | ÷ 16 = 8 | ÷ 4 = 32 | ÷ 4 = 32 | `0x4fa550` |
| serial | ÷ 16 = 8 | ÷ 10 = 12 | ÷ 10 = 12 | `0x4fa780` |
| defaults | 2 | 4 | 6 | `0x4f5130` |

A data packet sent gets the +0x2E count (`0x51c958`); a request for a
missing packet (a control with sequence 0x7F or below) the +0x2C count
(`0x51cad3`); a packet received starts an acknowledgement count of
+0x2A (`0x51c820`). The counts go down by 1 at each service of the link (`0x51caf0`), not by the clock; at 0 the
packet is sent again (`rsnd`). In the IPX capture the 18 packets sent
again went 240-260 ms after the first copy.

The window is the number of send buffers, 2^(n−1) − 1 for an n from 2
to 7 (`0x51d30c`-`0x51d33f`, `0x51d53b`); with none free a send fails
(`nobuffers`, `0x51d868`). In the IPX capture a copy got 63 packets
ahead of the last acknowledgement it had, so n is 7 there: 63 packets,
half the 7-bit sequence.

## Serial

Over a serial link (a null-modem cable, stood in for as TCP) the game
first looks for a modem: `+++`, then `ATE1` and CR, `AT` and CR. With
another copy at the other end instead, it goes on to the reliable layer
of IPX's data packets, without the `ea dat` tag: the 6-byte header
(0xFE, length, sequence, acknowledgement, BE checksum) and the game
message. The game messages are those of TCP, without the length:

- 6 bytes, an acknowledgement alone (`fe 00 fb ..`);
- 26: message 07 (`0x4c4310`), from both ends: +4 u32 the frame count
  `0x79c1a4` (0x15B2 and 0x15BB in one run), +8 the name; +1-3 are
  whatever the buffer held;
- then from the host 01 (130 = 6 + 124) and 02 (46), often read
  together (176), then each player's 03 (102), the barriers and the
  set-up (05);
- in a race, 06 from each end about 30 times a second (12 = 6 + 6).

**Who hosts.** Over a serial link (`0x7a22e0` = 2) the end whose frame
count is higher hosts (slot 0); the other joins as slot 1; on a tie the
names decide (`0x4ee310`: the greater hosts), and equal names fail
(`0x4c438b`-`0x4c440f`). So the copy started first hosts. All four
serial captures agree (s5-s8). Over a modem (`0x7a22e0` = 1) the
comparison is skipped: the host is chosen before the call (`0x7a22da`,
`0x4c4039`). In both modem captures copy A hosted, though in m7 its count
was the lower.

## What isn't known

- Most of the player and set-up records' numbers (03, 05); what
  `opcars` 0-3 mean.
- Input bit 0x80's action (key S; perhaps a cop's call), and the
  meaning of 09's flags beyond how `0x43c000` makes them.
- 08-12 were read from the code only; no capture paused, quit or
  downloaded a car. The u32 at +0x14 of 0f/11.
- How often the link is serviced, so the resend time in ms; and which n
  TCP and the serial and modem links pass (63 is from the IPX capture).
- The fields of `dec`, `add` and `rsp`.
