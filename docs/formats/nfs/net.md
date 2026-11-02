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
  IPX (carried over UDP, [nfs.md](../../engines/nfs.md)). Later runs over
  TCP paused, continued, restarted and quit from the pause menu, held
  each control key in turn, and had a copy without the other's car; runs
  over serial and modem links lost packets on purpose (`-netdrop`).
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
| `03` | 96 | a player, kept at `0x79f5f0` + slot × 0x60 (below). A copy ignores its own; the host passes others' on | `0x49b310` |
| `04` | 4 | a barrier: +2 u16 its number (1, 0x0f, 0x10, 0x17, 0x18, 0x1f, 0x0d, 3-6 seen at a race's start; 0x0c abandons); +1 is whatever the buffer held. The host sends, each player answers with the same, within a timeout in seconds (`0x4a6a10`'s first argument, × 128 ticks) | `0x4a6c40` |
| `05` | up to 209 | a piece of a block, appended (the race's set-up: 2,192 bytes in 11 pieces; below) | `0x4c6080` |
| `06` | 33, host to all | one race frame: 8 u32 input words, one for each car | `0x4c61e0` |
| `06` | 6, player to host | +1 s8 how many frames ahead (0-4 seen; down to −15 while the host's frames were lost); +2 u32 its input word (sent by `0x4c60c0`) | `0x4c61e0` |
| `0a` | any | chat: NUL-ended text (`Player1: hi`); the host passes it on | `0x42d710` |
| `07` | 20 | link set-up over serial and modem (`0x4c4310`): +4 u32 the frame count `0x79c1a4`; +8 the player's name (`Player1`). Stored at `0x7d3650` (Serial, below) | `0x4c4190` |
| `08` | 12 | the pause menu: +4 u32 an operation, +8 u32 a frame (below); +1-3 whatever the buffer held | `0x4c5b00` |
| `09` | 16 | +1 the sender's car; +4, +8, +0xC three u32 flags for its steering, gas and brake bindings, kept in the set-up's car (+0x14-0x1C, `0x6fd550` + car × 0x6C). `0x43c000` gives 1 unless the control is bound to an input device's axis (binding kind 1) and that device's record (`0x6fc970` + n × 0x88) lacks flag 4; so 1 for keys | `0x43dab0` |
| `0b` | 14 + 2 per AI car | a status every 32 steps (`0x4c7bd0`, [nfs-threads.md](../../engines/nfs-threads.md)). Positional updates only (below) | `0x4c7d30` |
| `0c` | 196 | +1 a car; +4 0xC0 bytes of the car's race record (car + 0x224: finish time, laps), copied in. Sent at the race's end (`0x4c7ea0`, from `0x455423`, the results), the local car's to the host and the host's for every car, between barriers 0x22 and 0x23 (90 s). Positional updates only | `0x4c7f90` |
| `0d` | 2 | +1 a car that has finished: its +0x2C4 is set to 3 (`0x4c7e50`). Sent with each 0b once the local car has finished (`0x4c7d09`). Positional updates only | `0x4c7e80` |
| `0e` | 18 | +2 eight u16, one per slot, to `0x7a22ca`: 2 for a player the host has dropped (`0x49ecf0`); seen when a player quit (`0e` `0f`, then 0, 2, 0...) | `0x49edb0` |
| `0f`, `11` | 28 | a car offered: +1 a 16-byte key, compared with each car's +0x31C (`0x439990`; the Corvette's is 0x13 and zeros, its serial number); +0x14 u32 the car's flags (car +0x380, below); +0x18 u32 the size of its `carp.txt`. `0f` goes from a player to the host, `11` from the host to the others (Car download, below) | `0x49b3e0`, `0x49b470` |
| `10`, `12` | up to 209 | the next piece of that car's data (the bytes after the type), 208 at most | `0x49b500`, `0x49b520` |

Sizes for 07-12 are what the senders pass to `0x4a60e0`. All but 0b, 0c
and 0d are in the captures: 07 over serial and modem, 08 and 0e from the
pause menu, 11 and 12 from a car download. 0b, 0c and 0d are sent only
with `0x6fd50c` set (Positional updates, below).

**Positional updates** (`0x6fd50c`). The texts the host dialog loads
(`0x43f325`, texts 27-34) name two pairs of choices: Peer-to-Peer or
Client/Server (`0x7a22e4` 1 or 0) and "Positional Updates" ("best for
slow network connections and Internet") or "Controller Updates" ("best
for fast, local area networks"). `0x6fd50c` is the first of the second
pair: the dialog starts it at 0 (`0x43f43e`-`0x43f45e`, Controller) and
its handler can only clear it (`0x43f927`). No dialog shows the second
pair. The dialog code `0x445750` has one caller, `0x43f4cb` in
`0x43f320` (whose callers are `0x43f65f`, `0x43f7a2` and `0x43fbc4`),
and is given 5-entry lists: texts 27, 28, -, 29, 30, their
descriptions 31, 32, -, 33, 34, and the variables `0x5f3fe4`,
`0x7a22e4`, -, `0x6fd50c`, `0x5f3fe0` (`0x5556e0`). But the count it
gets is 2 (ebx at `0x43f4b2`), and it draws and checks only that many
(`0x4453ac`). So the TCP Host Game dialog shows only the first pair
(seen in a run), and so do the others. The one place that sets it
to 1 is `0x49e080`, reached at the first front-end entry (`0x473218`,
`0x55b03c` starts at 1) when the command line gave a game: `-IP=`,
`-PORT=`, `-NAME=`, `-PLAYERS=`, `-PNUM=`, `-RACE=`, `-TRACK=`, `-CAR=`,
`-LAPS=`, `-LEVEL=`, `-CLASS=`, `-AINUM=` (table `0x55ec50`, parsed by
`0x4a5080` into `0x7a1fa0`-`0x7a2018`; "You must specify ALL of the
following information: Rebroadcaster address, Player Name, Player ID,
Number of Players"). That is a launch from EA's online service: it sets
`0x7a22e0` 3, `0x7a22e4` 2, `0x6fd50c` 1, `0x6fd3b0` 4, hosts if
`-PNUM=` is 1 or less (`0x49e2e0`) or else joins (`0x49e220`), and
connects through `NPSConnectToServer` of an NPS client DLL
(`0x4a54f0`, `0x4a5b00`; the default server `HPEREZ2.eac.ea.com`, port
6666), which the CD doesn't have. With it set the race isn't lockstep:
each copy takes only its own car's frames (`0x4c67c6`), stores its own
word without waiting for the host (`0x4c6154`), the other players' cars
are driven by `0x40bf50` from their 0b statuses (`0x42c4c4`), and the
results are made to agree with 0c. Where the front end would set Hot
Pursuit it sets Knockout instead (`0x452b11`).

**Captured with positional updates.** In a copy of the game (`nfs3.exe`
patched: `0x43f43e` and `0x43f451` store 1, so Connect Players turns
`0x6fd50c` on), two copies raced Hometown over TCP as before. No 06 went
either way: after the barriers each copy sent only 0b, every 32 steps
(the step at +4 going 0x20, 0x40...; about 2 a second), the player's 14
bytes (+1 slot 1, +8 s16 speed, +0xA u16 race distance, +0xC byte 3 of
its word), the host's 26 with six u16 for the computer's cars (7, 0xA,
0xD... on the grid, each car's distance). With a second patch that
makes every racer finish at once (`0x4ce3f5` and `0x4ce48d` skipped),
each copy sent 0d with its slot after every 0b (`0d00` from the host,
`0d01` from the player); at the race's end, between barriers 0x22 and
0x23, the player sent its 0c (car 1) and the host all eight. 0c's
0xC0 bytes are the car from +0x224: +0 the finish distance (laps ×
road slices, 0x77E), +8 the slice (1, 4, 7...: the grid), +0x14 the
step the lap began less 512 (`0x4ce4c7`; -512 here), +0x98 the place (car +0x2BC), +0xA0 the state (3,
finished).

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
| 6 | 1 | `0x6fd4d4` | `opcars`: the opponents' car: 0 "None" (text 193), 1-3 Class A, B, C (texts 190-192, set at `0x43a34f`); 4 and up is a car (its number + 4) | 2 |
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
`0x44c095`, `0x44c23b`). The lobby showed "Opponent Car... Class B" for the captured 2.

**Pausing** (08, `0x4c55a0`, `0x4c5720`). The u32 at +4:

| +4 | Sent by | Meaning |
|---|---|---|
| 0 | a player who pauses (Esc), to the host, +8 0; the host sends it to all | pause. The host picks the frame (frames stored + 2, `0x4c6970`) and sends it at +8 (0x25E, 0x385 seen); when each copy reaches it, it stops sending controls (`0x560108`) and waits at barrier 8 (for up to 30 s) |
| 1 | a player choosing Continue, to the host (`0x4c5a68`) | continue: the host notes it (`0x7d36c4`) and ends the pause with barrier 9 |
| 2 | nobody: of the six calls of `0x4a60e0` with 12 bytes, `0x4c5637` and `0x4c566d` send 0, `0x4c5a7a` 1, `0x4c5aa7` 3, `0x4c5b53` passes a 0 on, and `0x4a6611` is barrier 0x0c | a player takes state 0xA (`0x4c5ba5`), which ends the pause with barrier 0xA |
| 3 | a player choosing Quit, then Game Setup, to the host (`0x4c5a90`) | quit: the host marks that player's slot 0xB and drops it (`0x49ecf0`, which sends 0e) |

The host's own choices send no 08: it goes straight to the barrier. The
host's pause menu has Continue, Restart and Quit (Quit to: Main Menu,
Race Results, System); a player's has Continue and Quit (Quit to: Game
Setup, System). Each choice asks again (Yes, No) but Continue. The
menu's five outcomes (`0x4c5883`-`0x4c5990`, table `0x4c5704`), as
captured over TCP:

| Outcome | Choice | Barriers | Then |
|---|---|---|---|
| 1 | Continue | 9, then 1 | the race goes on |
| 2 | Restart (host) | 0xA, then 5 and 6 | the race starts again, without a new set-up (05) |
| 3 | Race Results (host); Game Setup (a player, after 08 with 3) | 0xB, then 7 | the lobby: each 03 and the 02 sent again |
| 4 | Main Menu (host) | 0x20, then 7 | the lobby, as for 3 |
| 5 | System | 0xB | the session is abandoned (`0x7a22dc`) and that copy quits |

Pauses are at least 32 frames apart (`0x7d36cc` + 0x20). In the second
argument of `0x4a6a10` is the barrier; the first, 0x1E for the pause and
0x14 for the others, is its timeout in seconds.

**The player** (03, built at `0x49a8e0` into `0x79f918`):

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | the type, 3 |
| 4 | 4 | the slot |
| 8 | 1 | ready (`0x6fd508`) |
| 9 | 11 | the name (`0x6fd2fc`; the online launch's `-NAME=` takes 11) |
| 0x14 | 1 | the car chosen, its place in the car list (`0x6fd2bc`) |
| 0x15 | 1 | transmission, 1 automatic (`0x6fd2d4`) |
| 0x16 | 3 | the three flags of 09, one byte each (`0x6fd2dc`-`0x6fd2e4`) |
| 0x19 | 16 | the car's key (car +0x31C; 0x13 for the Corvette) |
| 0x29 | 16 | make (`0x439ab0`) |
| 0x39 | 16 | model |
| 0x49 | 4 | the car's colour (`0x6fd307`-`0x6fd30a`; `70 00 00 77` captured) |
| 0x50 | 4 | the car's flags (car +0x380; 0x42 for the Corvette) |
| 0x54 | 12 | the car's half size in x, y, z (1.18326, 0.668848, 2.40644 for the Corvette): front-end car +0x394 (`0x439510`), read from `car.fce` +0x28 (`0x43b4b0` → `0x49c700`, `0x49c7fb`-`0x49c810`) |

**The race set-up** (05) is a copy of memory from `0x6fd39c`, 2,192
bytes: the race's variables, then 16 cars of 0x6C bytes at `0x6fd52c` +
car × 0x6C (the record a race car points to at +0x220, `0x42c780`; the
host keeps a copy of all 16, `0x4c5ee9`), then the starting order (8
u32 at +0x850, `0x6fdbec`). The table of cars below counts from
`0x6fd53c`, 0x10 into each record, as it was first read; add 0x10 for
the record's own offsets. The header's
words are the variables at `0x6fd39c` + offset: +0x18 the seed
(`0x6fd3b4`), +0x14 the kind of play (`0x6fd3b0`, 4), +0x118 the laps,
+0x134 `oppnumber`, +0x138 `opcars`, +0x168 `classLimiter`, +0x16C ready
(`0x6fd508`), +0x174 the cars (`0x6fd510`, 8), +0x178 the players
(`0x6fd514`, 2), +0x17C the human racers (`0x6fd518`), +0x184 the
computer's racers (`0x6fd520`, 6). The others:

- +0xC (`0x6fd3a8`, 2): set to 2 by the settings' defaults
  (`0x472661`, `0x472d7f`) and never read.
- +0xF0 (`0x6fd48c`, 1 by default, `0x4724e4`): copied at a race's
  start to `0x6fd4a8` (`0x4746c3`), which, in a one-player game after
  0x480 steps, puts a cop being reset on the slice behind the racer it
  is after (`0x405288`-`0x4052b7`). It is the option **`copassist`**
  (row `0x556668` of the option table `0x5563fc`, values On/Off like
  the other driving aids, texts 228/229), the cops' assist. At a Hot
  Pursuit's set-up the other aids' race copies are zeroed and only this
  one is copied (`0x474640`). The main menu's assists button
  (`0x45d15a`-`0x45d1c7`) reads "Assists On" (text 0x196 = 406) when
  `braking`, `collision`, `steering`, `traction`, `bestline` or
  `navigator` is on, or in Hot Pursuit `copassist`; else "Assists
  Off" (407).
- +0x150 (`0x6fd4ec`, 1, `0x4724db`): no reader found.
- +0x190 is not a header field: it is the first car record's +0
  (`0x6fd52c`). Each record's **+0-0xF is the car's 16-byte key**
  (front-end car +0x31C, `0x439520`; 0x13 and zeros for the Corvette,
  2 and 0xF for cars 3 and 8 in the capture), or 16 bytes of 0xFF for a
  car number outside 0-0x31. `0x4781b0` writes them for the
  `0x6fd510` cars, given the settings block `0x6fbb40`, of which the
  records are part (`0x6fbb40` + 0x19EC): at a race's set-up
  (`0x474d17`) and into the replay's copy before it is saved
  (`0x4bd290`, `replay.rp3`, block `0x7a3e50`). `0x478100` turns the
  keys back into car numbers (`0x439990`) when such a block is read
  (the replay's loaders `0x4bd368` and `0x446116`-`0x446474`; also
  `0x477bad`), failing on an
  unknown key unless the number is 50-599. Checked in a run
  (`gh2-run1`, single race, `-dumpmem` after `0x474d17`): records 0-3
  held 13, 02, 0F and 13 then zeros, for car numbers 1, 3, 8 and 1.
  So the key lets a replay or
  saved setting find its car when the car list differs. Nothing in a
  race reads it: its only other use is the copy in 05 (found by every
  access to the records' first 16 bytes, Ghidra and the capstone pass).
  The table's own entries:

Each car (filled for players at
`0x49acf1`-`0x49adeb` from their 03):

| Offset | Contents |
|---|---|
| 0 | the slot (0-7) |
| 4 | the car's number (from 03's key, `0x439990`) |
| 8 | 1 a player, 2 the computer |
| 0xC | transmission (03 +0x15); 1 makes the driver shift itself (`0x43149a`) |
| 0x10 | the colour (03 +0x49) |
| 0x14, 0x18, 0x1C | the flags of 09 (03 +0x16-0x18); 1 for players, 0 for the computer's cars |
| 0x20 | the driver's number (2-6 for `Gotcha`, `Ram Rod`, `Dead Beat`, `Mulligan`, `Wild One`; the players had 0 and 6) |
| 0x24 | the name (12: 03's 11 and a zero) |
| 0x30, 0x34 | the set-up sliders (7 floats from `0x55d024`, `0x473d8d`; the record's +0x40, physics.md): Engine Tuning 1.0, Brake Balance 0 |
| 0x38 | the other five sliders, 1.0: Steering Speed, Gear Rat, Suspension, Aero, Tires |
| 0x4C | three floats, 03's +0x54, the car's half size (players only, `0x49ade8`); no reader found |
| 0x58 | the car's flag 0x08 (left out of the lists, `0x4395a0`, `0x473db1`); the host keeps its own (`0x4c5ff0`). Read at `0x4cf066` |
| 0x5C | the next record's +0-0xF: its car's key (above) |

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
| 3 | 0x80 | spike strip (a cop's: below) | S | car +0x512 |

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

**Checked by capture.** Copy B held keypad 8, keypad 2, keypad 4,
keypad 6 and Space for a second each, and pressed A, Z, H (held a
second), L and S, a second apart. Its 06 to the host carried, in turn,
`00ff0000`, `0000ff00`, `80000000`, `7f000000`, `00000004`, then
`00000010`, `00000020`, `00000008`, `00000040` and `00000080`, each
press for 3-4 frames (about 100 ms, the script's press); the host's
frames carried each as car 1's word, unchanged. So steering is −128 to
127, gas and brake 0 or 255 from a key, and the bits are as above.

**Bit 0x80, spike strip.** The Controls menu names the 13 bindings from
the jump table at `0x43c8c8` (`0x43cbe4`-`0x43cc38`): text 0x46-0x51
(`kTextKeyLeft`...`kTextKeyReset`), and for the 12th, S, text 0x1BC,
`kTextKeySpikeStrip` ("Spike strip"). The bit is acted on at `0x404e40`,
called only from a cop's states (`0x405860`, `0x4058ad`, next to the
siren's `0x404f80`): once per press (car +0x513), if the cop
(car +0x714, its own index in the car list, set at `0x4012e3`) has no
strip out (`0x7dd850` + car × 0x50), it lays one on
the track at the cop's place (`0x4c9dd0`, +0x1C) with an arming step
of step + 0xC0 (3 s), and plays radio speech (`0x40b9c0`) and sound
0x10. A press before that step gives sound 0x12 and nothing else; a
press after it clears the strip (`0x4c9f50`) with sound 0x11. The
sounds play only for the car on screen. In a race (not a pursuit) the
press is sent and nothing happens.

The strip doesn't go away by itself. Until the arming step it blinks
(`0x4c9f70` draws it only while bit 0x10 of the steps left is clear)
and catches nothing; from then on it is drawn solid and stays until it
is cleared: by the cop's next press, by the ticket (`0x4040e0`, for a
human cop), or by a racer driving onto it. Each step a racer still in
the race and on the ground is tested against armed strips within one
slice (`0x4297d0`, `0x4ca160`, the four wheels against the strip's
quad). A hit clears the strip, sets the racer's +0x99C to the wheels
that touched it and +0x621 bit 0x40, and `0x401650` marks the racer's
chase record pulled over (+0x1C = 2); a human cop without a target
then takes it as one if it is within 20 slices and speeding (or
already chased). The cop's
HUD row of eight spikes is not drawn while the cop has a strip out
(`0x483ef3`; in nfs-host, `cop-spike2`, it went at the press and came
back at the clearing press 14 s later). The map shows the strip
(`0x48d4c0`).

A strip hit, run (`cp-spike1`, November 2026, nfs-host with the cop
pokes; physics.md has the cop's states). The cop pressed S once, in
state 3 on the grid at 43.1 s, with the siren off, and drove nowhere.
`0x4c9dd0` laid it at the cop's slice 957, lane 9 (the right half of
the road: the half is picked by lane < 7), about 12 m across, with
the arming step 346 = the step then (154) + 192. The racers came round
on lap 2: at 190.09 s (step 9562) Gotcha (car 6) ran over it at
42 m/s. The same step `0x4297d0` cleared the strip (`0x4c9f50`), set
its +0x99C to 0xC (wheels 2 and 3) and called `0x401650`, which set
its chase record's +0x1C = 2 and, the cop having no target and the
racer being over the speed (+0x6F0 20.3) and within 20 slices, made it
the cop's target (`0x4018b0` with 3), siren on. Then:
- 190.13 s: the cop to state 4, the racer (computer-driven) to its
  state 5, pulling over.
- 193.83 s: the racer under 3 m/s, 85 m on, in its state 6; the cop
  to state 5 at once (`0x4058bf`), 59 m away: no 7 m test for a racer
  already pulled over.
- 196.89 s (192 steps): the ticket, the racer out (+0x2C4 4, coasting
  in `0x407380`), the cop back to 3. +0x99C was 0 again by 228 s.
- The HUD's spike row was gone from the press until the hit and came
  back with it; the racer list showed "Gotcha 0 tickets 84 MPH - 45
  MPH over" in yellow (frames 6036-6042 of `fr-cp-spike1`).
The spiked wheels' effect (`0x42a560`): sound 0x13 on the first step
and a puff at each spiked wheel (`0x4cb010`, effect 0xD) for two
steps, while +0x621 bit 0x40 holds; other readers (`0x42a130`,
`0x471860`, `0x4bab70`) aren't followed. A racer crossing the strip
before its arming step wasn't run.

The recorded game (one-player cop footage, physics.md) dims the row
for about 3 s instead. In this exe the row is a single quad added to
or taken out of the HUD's quad list `0x749a38` (`0x49bd30`,
`0x49bdc0`) by whether a strip is out, with no dim state, so it is
gone, not dimmed, under any renderer; the footage is probably another
build.

Seen in a run (October 2026, `nfs-host -poke`, docs/engines/nfs.md): a
single-player Hot Pursuit with the player in the Pursuit Corvette
(race type `0x6fd3b8` = 3 and player 1's car `0x6fd2bc` = 12, written
late, at `0x473bf0`, so the player was really a racer, state 2 of the
police states `0x405740`). The strip is laid only in states 3 and 4.
With car +0x6E0 (the state, `0x5e1800` for that car) set to 3, each S
press laid a strip across the road, the next found it still out, and
the one after cleared it. Poked in time (`0x474ca0`) the player's cop
starts in state 3 by itself, and chases once its siren (H) is on and a
racer near is speeding ([physics.md](physics.md)).

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
ticks (3.5 s) is dropped. Others the code knows: `ply`, `req`, `get`, `no!`,
`bye`, `out` (sent when a player left).

The joining messages share a head after the tag: +4 a BE u32, the
asker's number for a question (from `0x5689dc`, counting from 1, by
`0x513190`, which also waits 2 × 128 ticks for the answer) or the
question's number in an answer; +8 the game's IPX address (network 4,
node 6, BE socket 2); +0x14 8 bytes, zero. Then:

| Tag | Bytes | After the head | Built at |
|---|---|---|---|
| `dec` | 176 | +0x1C the game's GUID (BE u32, u16, u16, 8 bytes: the one in its `EAGAME.` name); +0x2C its name (32, `Player1`); +0x4C-0x5F the game's record +0x74-0x87, which is the game's own info (`NET_newgame`'s, `0x7a0420` here) +0x3C-0x4F: 4 bytes, BE u16, u16, u32, u32 and +0x5C BE u32 the most players, 8 (`0x7a046c`: 2 for Client/Server, 8 for Peer-to-Peer, `0x49e6be`; `0x5132a0` stops there). The game never writes `0x7a045c`-`0x7a046b`, so +0x4C-0x5B are 0: room left for the application. +0x60-0x6F and +0x74 on aren't written by the builder (stack). +0x70 BE the mask of player rows in use (game +0x50, 0 before anyone joined). A copy finding the game reads them back the same way (`0x513a5e`-`0x513bcb`) | `0x513030` |
| `add` | 140 | +0x1C a 16-byte string (zero); +0x2C 0x5C bytes of the joining player's record (+0x30 of its game object): its IPX address at +0x2C, its name at +0x74 (`Player1`); +0x88 is whatever the stack held (`fd9d64ff`) | `0x5144b0` (NET_setgame) |
| `rsp` | 36 | +0x1C BE 1, taken; +0x20 BE the player's row in the game's table (0x64-byte rows at game +0x88), 0 for the first | `0x5137b9` |

How found: the captures' bytes, then the builders: `dec`'s fields at
`0x513056`-`0x51313c`, `add`'s at `0x5144ea`-`0x514551`, `rsp`'s at
`0x5137ec`-`0x51381c`.

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

**Car download** (0f-12, `0x49bae3`-`0x49bc64`). At a race's start,
after the set-up, each copy offers the cars that came with neither flag
0x01 nor 0x40 (`0x439530`). The flags (car +0x380, `0x43a0f4`-`0x43a144`)
come from words of the car's `fedata` file in its `car.viv`: +6 gives
0x01 (folders `Elni`, `Jxjr`, `Merc`, `Lcop`, `Peln`: the bonus cars),
+8 0x02, +0xE 0x04, +0x10 0x10 (`Lcop`, `Peln`, `Vcop`: the police cars),
+0x14 0x40 (every car on the CD but those five and `Knoc`). So of the
CD's cars only `Knoc` could be offered; cars added to an install would
be. The others: +8, flag 0x02, is set for every CD car but `Elni`,
`Knoc` and `Peln` (`fedata.eng`), and is read only by car filters that
nothing seems to use: `0x4390f0`'s masks 2 and 4 (its one caller,
`0x4391d0`, has no direct call) and `0x43a6e0`'s bit 0x20000 (no
caller passes it).
+0xE, flag 0x04, is set for no CD car: such a car's front-end pictures
(`IN_%s.fsh`, `SL%s01.qfs`, `H%s.qfs`, `SH%s.qfs`) are read from its
own folder (`%s%s\`, car +0x315) rather than the shared one
(`0x4530c0`-`0x453330`), as an added car would need. Two flags come
from the code, not `fedata`: 0x20 for a car whose make is `Ferrari` or
`Mercedes` (`0x43a22e`, `0x43a255`); 0x08, a car left out of the lists
(`0x439109`).

**The car lists.** `0x43a6e0` takes a mask: bit 8 picks the `cars`
list, bit 4 the `opcars` list, the other bits what to clear in them
(0x2000 the cars with flag 0x20, `0x43aa56`; 0x4000 the police cars,
0x10, `0x43aaa1`; 0x80-0x800 `opcars`' four choices before the cars,
0x800 all its cars, `0x43abd3`-`0x43ac20`). The front end's option
table (`0x5563fc`) gives the lists' owners: `cars` (`0x55500a`, a u16
per car) is shared by `cars`, `car2`, `carShowcase`, `carCompare1` and
`carCompare2`, so by Player Car's lists; `opcars` (`0x55519a`, four
choices then a u16 per car) by the opponent's. `0x44be60` remakes both
on each pass of the front end's loop (`0x44b382`): all on, then
0x4004 (no police among opponents), then 0x4008 outside Hot Pursuit or
0x2008 in it (`0x44c0f7`-`0x44c119`). In Hot Pursuit `opcars` then
gets 0x2084, or 0xF04 (empty) in split screen unless both players' cars
are police cars, and in a network game unless `0x7a2274` >> 16 equals
`0x55e5d0` (`0x44c11e`-`0x44c19f`). A choice left on a
cleared entry moves to the next one on (`0x43ac61`). So in Hot
Pursuit the Ferraris and the SL600 leave both lists. Seen in a run
(`nfs-host -dumpmem`, race type set to 3 at 19 s in the main menu):
`cars` went from cars 0, 1, 3, 4, 5, 7, 8, 9, 11 to 0, 1, 5, 7, 8, 9,
12 (the F355 3, the 550 4 and the SL600 11 out, the Pursuit Corvette 12
in) and `opcars` lost 3, 4 and 11 and its first choice.

**When a Hot Pursuit list can still hold the Ferraris** (November
2026, every path through `0x44be60` and the writers of the race type
`0x6fd3b8`). The rebuild runs on every pass of the front end's loop
(`0x44b382`, after the screen's handler), so a race type changed in a
menu (`0x440160`, `0x452ac0`, `0x452f80`, `0x45dac0`) or arriving from
the host (`0x49a805`) takes effect on the next pass. Three things leave
them in:

- the car viewers: on the `showcase` and `interior` screens the `cars`
  list is set all on again and then cleared only by mask 0x80008 (a
  test of the car's pictures, `0x4530c0`), on `history` by 9 and
  0x280008, on `compare` set all on alone (`0x44c1a4`-`0x44c236`), whatever the race type, so
  the Ferraris, the SL600 and the police cars are all in it there; the
  next pass on another screen clears them again;
- `postgame` skips the rebuild (`0x662968`, set at `0x44b25f`), so its
  lists are the last ones made;
- a race type set after the front end: the pokes of the Hot Pursuit
  run above (spike strips) set it only at the race's set-up
  (`0x474ca0`, `0x473bf0`), so the lists, and the car picked from
  them, are a single race's.

The earlier run's Ferraris are most likely the third (its Hot Pursuit
came from pokes), else a viewer screen; no path through the menus
leaves them in Player Car's or the opponents' list in Hot Pursuit.

(`0x4391d0` and the bit 0x20000 have no direct
caller or user.) In turn, between barriers 0x0F + slot, each
player offers its own car to the host (0f, 10); then between barriers
0x17 + slot the host offers each player's car to all the others (11, 12).
The data: the car's `carp.txt` (the size at +0x18) in pieces of 208
bytes, then 300 bytes, its full name (`Chevrolet Corvette`, from
`0x439250`).

How found: copy A's Corvette had its `fedata` +0x14 cleared, and copy B
had no Corvette. A sent 11 (key 0x13, flags 2, size 5,027), 24 pieces of
208 and one of 35 (the 5,027 bytes of `carp.txt`), then 208 and 92 (the
name), all within 3 ms. B wrote `GameData\CarModel\Carp.txt`, made a
car folder `P000` (it tries names until creating `exist.nam` in one
fails, `0x4383f0`), built a `car.viv` there of the `carp.txt` and the
name, raced with it, and removed the folder when it quit. A copy that
has the car (`0x439990` finds the key) takes the pieces and ignores them.

**Timers and window** (`cmn\packet.c`). Each transport sets three u16
when it opens a link, as fractions of `0x5643d8` (128), but only those
the caller left at 0:

| Transport | +0x2A acknowledge | +0x2C ask again | +0x2E resend | Set at |
|---|---|---|---|---|
| IPX | ÷ 30 = 4 | ÷ 15 = 8 | ÷ 15 = 8 | `0x514ec0` |
| modem | ÷ 16 = 8 | ÷ 4 = 32 | ÷ 4 = 32 | `0x4fa550` (but see below) |
| serial | ÷ 16 = 8 | ÷ 10 = 12 | ÷ 10 = 12 | `0x4fa780` |
| defaults | 2 | 4 | 6 | `0x4f5130` |
| the game's IPX links | ÷ 4 = 32 | ÷ 4 = 32 | ÷ 4 = 32 | `0x49efb0` (from `0x49e8cd`, `0x49ebec`), before the open |

So over IPX the game's own 32 counts win: the transport's 4 and 8 never
apply to its players' links. Serial and modem links are given theirs
too: just before the open, `0x4c4230` fills the parameters at
`0x7a22e8` that `0x4fa550` and `0x4fa780` get, +0x2A 128 ÷ 16 = 8 and
+0x2C and +0x2E 128 ÷ 5 = 25 for a modem (`0x7a22e0` 1) or 128 ÷ 10 =
12 for serial (`0x4c42a0`-`0x4c42db`). So a modem link resends after
25 ticks, not 32, and serial after 12 as in the table.

A data packet sent gets the +0x2E count (`0x51c958`); a request for a
missing packet (a control with sequence 0x7F or below) the +0x2C count
(`0x51cad3`); a packet received starts an acknowledgement count of
+0x2A (`0x51c820`). The counts go down by 1 at each service of the link,
and at 0 the packet is sent again (`rsnd`).

**The service** is the reliable layer's entry +0xC, `0x51d8f0`. Opening
a link whose layer has one (`0x4f5130`; TCP's hasn't) adds `0x4f5110`
to the network thread's handlers (`0x4f2930`, table `0x9f0c00`); that
thread (`0x4f27a0`) runs them each time the 128 Hz timer callback sets
its event (`0x48ee1b` → `0x4f2a60`), and `0x4f5110` calls the service
of every open link (list `0x9f2628`). So a link is served once a tick,
every 7.8 ms, on the network thread, over serial, modem and IPX alike.
Each service takes 1 from the 15 s keep-alive count (+0x24, 1,920
ticks); if the transport says the link can send (+8: IPX `0x4f56c0`,
serial and modem `0x4fa480`), it takes 1 from each send buffer's count
(`0x51d010`), resends those at 0 (`0x51d040` → `0x51c8f0`) and counts
down the acknowledgement (+0x2C of the link, an `fb` control at 0,
`0x51d9c4`). In ms: a count n is n × 7.8 ms, so 12 is 94 ms (serial),
25 is 195 ms (modem) and 32 is 250 ms (the game's IPX links).

Checked at run time (IPX, `-watch` on copy B): from the link's opening
at 73,248 ms, 300 services in 2,360 ms (7.9 ms apart), 299 of them
counting the buffers down. In the race that followed, the packets sent
again went 247-251 ms after the first copy, and again every 250 ms
(499, 749... ms) while unacknowledged.

The window is the number of send buffers, 2^(n−1) − 1 for an n from 2
to 7 (`0x51d30c`-`0x51d33f`, `0x51d53b`); with none free a send fails
(`nobuffers`, `0x51d868`). In the IPX capture a copy got 63 packets
ahead of the last acknowledgement it had, so n is 7 there: 63 packets,
half the 7-bit sequence. The serial and modem links are opened with n =
7 too (`0x4c4048`-`0x4c408d`: n in ebx, 0xD2 the largest packet in edx;
n = −1 would take the layer without resends, table `0x56ac10`).

**Measured** (`-netdrop`, two copies racing, four copies running on four
cores; the clocks are each game's):

| Link | One packet lost | Every packet lost for 2 s |
|---|---|---|
| serial | the receiver asks for it (a control with its sequence) when the next arrives; sent again 33-46 ms after the first copy (29 of 29) | the oldest packet sent again every 93-95 ms (12 counts), an acknowledgement (`fb`) between; 8 packets ahead, then the game stopped making frames |
| modem | the same, 34-201 ms (46 of 47) | sent again every 195 ms or so (25 counts); 32 packets ahead before new ones stopped |
| IPX | | 240-260 ms (32 counts, the earlier capture); 247-251 ms, then every 250 ms (a later race) |

So the counts are of ticks, 7.8 ms each, on every link: serial's 12
counts are its 93-95 ms, modem's 25 its 195 ms and IPX's 32 its 250
ms. In a race the game, not the window,
stopped the sending: the lockstep stalls before 63 packets are out.

**TCP** has no reliable layer: its open (`0x4f7380`) gives the link
table `0x566e8c`, whose send is `0x4f7030` itself. That keeps one message
being sent; a send that would block (WSAEWOULDBLOCK, WSAENOBUFS) is
dropped and fails, and `0x4a60e0` tries again after sleeping 10 + 50 × i
ms. With every 50th send failing, the same message went 10-16 ms later
(9 of 9). How far ahead a TCP copy gets is the lockstep's: 0-4 frames.

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

- Whether the header's +0x150 is read through a pointer; why the
  players' driver numbers were 0 and 6. (Answered above: the records'
  +0-0xF are the cars' keys, the header's +0x190 is the first of them,
  and +0xF0 is `copassist`.)
- What an input device's flag 4 is (09's flags are 0 only for an axis of
  a device without it); no run used a joystick over the network.
- 0b, 0c and 0d were captured only from a patched copy: the unpatched
  game sends them only when launched by EA's online service, whose NPS
  DLL isn't on the CD. The Positional/Controller pair is built but never
  shown (count 2 at `0x43f4b2`); whether some build showed it isn't known.
- Spike strips weren't seen laid in a network game (footage of a
  one-player cop game: [physics.md](physics.md)). The way there was found but not
  finished: Hot Pursuit is in the lobby's race-type list (click the
  header at 240,77, then 208,108), and police cars are offered in Hot
  Pursuit (mask 0x2008), but the script didn't reach them in Player
  Car's list.
- Whether anything but the game's lockstep keeps a link under its 63
  buffers.
- What other EA games put in `dec`'s +0x4C-0x5B (zero from this one);
  `add`'s 16-byte string at +0x1C (zero in the capture: perhaps a
  password).
