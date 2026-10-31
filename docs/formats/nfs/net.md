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
| `03` | 96 | a player, kept at `0x79f5f0` + slot × 0x60: +4 u32 slot; +8 u8 ready (0, then 1); +9 name (32); +0x29 make (`Chevrolet`, 16); +0x39 model (`Corvette`, 16); the rest unknown. A copy ignores its own; the host passes others' on | `0x49b310` |
| `04` | 4 | a barrier: +2 u16 its number (1, 0x0f, 0x10, 0x17, 0x18, 0x1f, 0x0d, 3-6 seen at a race's start; 0x0c abandons); +1 is whatever the buffer held. The host sends, each player answers with the same, within a timeout in seconds (`0x4a6a10`'s first argument, × 128 ticks) | `0x4a6c40` |
| `05` | up to 209 | a piece of a block, appended (the race's set-up: 2,192 bytes in 11 pieces; below) | `0x4c6080` |
| `06` | 33, host to all | one race frame: 8 u32 input words, one for each car | `0x4c61e0` |
| `06` | 6, player to host | +1 s8 how many frames ahead (0-4 seen; down to −15 while the host's frames were lost); +2 u32 its input word (sent by `0x4c60c0`) | `0x4c61e0` |
| `0a` | any | chat: NUL-ended text (`Player1: hi`); the host passes it on | `0x42d710` |
| `07` | 20 | link set-up over serial and modem (`0x4c4310`): +4 u32 the frame count `0x79c1a4`; +8 the player's name (`Player1`). Stored at `0x7d3650` (Serial, below) | `0x4c4190` |
| `08` | 12 | the pause menu: +4 u32 an operation, +8 u32 a frame (below); +1-3 whatever the buffer held | `0x4c5b00` |
| `09` | 16 | +1 the sender's car; +4, +8, +0xC three u32 flags for its steering, gas and brake bindings (`0x43c000`), kept at `0x6fd550` + car × 0x6C | `0x43dab0` |
| `0b` | 14 + 2 per AI car | a status every 32 steps (`0x4c7bd0`, [nfs-threads.md](../../engines/nfs-threads.md)) | `0x4c7d30` |
| `0c` | 196 | +1 a car; +4 0xC0 bytes of the car's state (car + 0x224), copied in. Sent for every car between barriers 0x22 and 0x23 (`0x4c7ea0`) | `0x4c7f90` |
| `0d` | 2 | +1 a car that has finished: its +0x2C4 is set to 3 (`0x4c7e50`) | `0x4c7e80` |
| `0e` | 18 | +2 eight u16, one per slot, to `0x7a22ca`: 2 for a player the host has dropped (`0x49ecf0`); seen when a player quit (`0e` `0f`, then 0, 2, 0...) | `0x49edb0` |
| `0f`, `11` | 28 | a car offered: +1 a 16-byte key, compared with each car's +0x31C (`0x439990`; the Corvette's is 0x13 and zeros, its serial number); +0x14 u32 the car's flags (car +0x380, below); +0x18 u32 the size of its `carp.txt`. `0f` goes from a player to the host, `11` from the host to the others (Car download, below) | `0x49b3e0`, `0x49b470` |
| `10`, `12` | up to 209 | the next piece of that car's data (the bytes after the type), 208 at most | `0x49b500`, `0x49b520` |

Sizes for 07-12 are what the senders pass to `0x4a60e0`. All but 0b, 0c
and 0d are in the captures: 07 over serial and modem, 08 and 0e from the
pause menu, 11 and 12 from a car download. 0b and 0c are sent only with
`0x6fd50c` set, which no captured run had.

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
| 0 | a player who pauses (Esc), to the host, +8 0; the host sends it to all | pause. The host picks the frame (frames stored + 2, `0x4c6970`) and sends it at +8 (0x25E, 0x385 seen); when each copy reaches it, it stops sending controls (`0x560108`) and waits at barrier 8 (for up to 30 s) |
| 1 | a player choosing Continue, to the host (`0x4c5a68`) | continue: the host notes it (`0x7d36c4`) and ends the pause with barrier 9 |
| 2 | no sender found | the others take state 0xA, which ends the pause with barrier 0xA |
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
siren's `0x404f80`): once per press (car +0x513), if the pursued car
(car +0x714) has no strip out (`0x7dd850` + car × 0x50), it lays one on
the track at the cop's place (`0x4c9dd0`, +0x1C) for 0xC0 ticks, drawn by
`0x4c9f70`, and plays radio speech (`0x40b9c0`) and sound 0x10; a strip
still out gives sound 0x12, an old one is cleared (`0x4c9f50`) with sound
0x11. The sounds play only for the car on screen. In a race (not a
pursuit) the press is sent and nothing happens.

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

**Car download** (0f-12, `0x49bae3`-`0x49bc64`). At a race's start,
after the set-up, each copy offers the cars that came with neither flag
0x01 nor 0x40 (`0x439530`). The flags (car +0x380, `0x43a0f4`-`0x43a144`)
come from words of the car's `fedata` file in its `car.viv`: +6 gives
0x01 (folders `Elni`, `Jxjr`, `Merc`, `Lcop`, `Peln`: the bonus cars),
+8 0x02, +0xE 0x04, +0x10 0x10 (`Lcop`, `Peln`, `Vcop`: the police cars),
+0x14 0x40 (every car on the CD but those five and `Knoc`). So of the
CD's cars only `Knoc` could be offered; cars added to an install would
be. In turn, between barriers 0x0F + slot, each
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
half the 7-bit sequence. The serial and modem links are opened with n =
7 too (`0x4c4048`-`0x4c408d`: n in ebx, 0xD2 the largest packet in edx;
n = −1 would take the layer without resends, table `0x56ac10`).

**Measured** (`-netdrop`, two copies racing, four copies running on four
cores; the clocks are each game's):

| Link | One packet lost | Every packet lost for 2 s |
|---|---|---|
| serial | the receiver asks for it (a control with its sequence) when the next arrives; sent again 33-46 ms after the first copy (29 of 29) | the oldest packet sent again every 93-95 ms (12 counts), an acknowledgement (`fb`) between; 8 packets ahead, then the game stopped making frames |
| modem | the same, 34-201 ms (46 of 47) | sent again every 195 ms or so (32 counts); 32 packets ahead before new ones stopped |
| IPX | | 240-260 ms (8 counts, the earlier capture) |

So a service comes about every 6-8 ms over serial and modem, about every
30 ms over IPX; it isn't one clock. In a race the game, not the window,
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

- Most of the player and set-up records' numbers (03, 05); what
  `opcars` 0-3 mean.
- The meaning of 09's flags beyond how `0x43c000` makes them.
- 0b and 0c (never sent: `0x6fd50c` unset) and 0d, not captured; 08 with
  2, never sent.
- Spike strips weren't seen laid (that needs a pursuit with a player as
  cop); what fedata's flags 0x02 and 0x04 mean.
- What calls a link's service, and why it comes so much more often over
  serial and modem than over IPX; whether anything but the game's
  lockstep keeps a link under its 63 buffers.
- The fields of `dec`, `add` and `rsp`.
