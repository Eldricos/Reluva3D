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
| `02` | 40 | the race settings, kept at `0x79f8f0` (fields unknown) | `0x49b2b0` |
| `03` | 96 | a player, kept at `0x79f5f0` + slot × 0x60: +4 u32 slot; +8 u8 ready (0, then 1); +9 name (32); +0x29 make (`Chevrolet`, 16); +0x39 model (`Corvette`, 16); the rest unknown. A copy ignores its own; the host passes others' on | `0x49b310` |
| `04` | 4 | a barrier: +2 u16 its number (1, 0x0f, 0x10, 0x17, 0x18, 0x1f, 0x0d, 3-6 seen; 0x0c abandons). The host sends, each player answers with the same, within a timeout | `0x4a6c40` |
| `05` | up to 209 | a piece of a block, appended (the race's set-up: 2,192 bytes in 11 pieces; below) | `0x4c6080` |
| `06` | 33, host to all | one race frame: 8 u32 input words, one for each car | `0x4c61e0` |
| `06` | 6, player to host | +1 s8 how many frames ahead (0-3); +2 u32 its input word | `0x4c60c0` |
| `0a` | any | chat: NUL-ended text (`Player1: hi`); the host passes it on | `0x42d710` |

Types 07-09 and 0b-12 have handlers but weren't in the captures:
07 (20 bytes to `0x7d3650`), 08 (an operation in a u32 at +4), 09 (a u32
for each car), 0b, 0c (196 bytes to a car), 0d (a car's number at +1), 0e
(16 bytes to `0x7a22ca`), 0f-12 (names and numbers).

**The race set-up** (05): a header of 0x1A0 bytes, which repeats the
settings of 02, then 8 cars of 0x6C bytes (+0x20 u32 the driver's
number, +0x24 the name, `Ram Rod`, `Lead Foot`..., six floats of 1.0, the
car's codes), then the starting order, 8 u32.

**The race** is lockstep: no positions or speeds go over the network,
only each car's controls. An input word has a bit for each of 32 bound
actions (bit i for action i, built at `0x48f645`); each car keeps the last
128 frames' words (at `0x7d3714` + car × 0x200). A player sends its word
to the host each frame; the host sends every car's word for the frame.
For a player that's late, the host repeats its last word with byte 3
masked by 0x8D. Which bit is which action wasn't worked out.

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

## What isn't known

- The race settings' fields (02), most of the player and set-up records'
  numbers (03, 05), and which input bit is which control.
- How long the IPX layer waits before sending a packet again, and how
  many it sends before an acknowledgement.
- The messages not captured (07-09, 0b-12) and the fields of `dec`,
  `add` and `rsp`.
- Serial and modem links: whether they use the same reliable layer as
  IPX. The serial link is stood in for (a COM port over TCP,
  [nfs.md](../../engines/nfs.md)), and its traffic hasn't been read yet.
