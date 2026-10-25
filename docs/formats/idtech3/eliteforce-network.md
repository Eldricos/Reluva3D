# Elite Force: the Holomatch network protocol

Status: **implemented, not yet tried against the retail game**. This
page is a specification for a server and a client that talk to the
retail Elite Force Holomatch (patch 1.2) over the network. Reluva's
(`src/engines/idtech3/wire/`, `eliteforce/net_fields.cpp`,
`net_server.cpp`, `net_client.cpp`) is tested with Reluva at both ends,
in memory with datagrams lost and over the system's sockets, and was
played Reluva to Reluva on the retail data patched to 1.2 (October
2026); not yet against the retail executable. Not done: the pure check (`cp`: Reluva's
server is never pure and its client doesn't send one), downloads, rate
limits, the master servers, and leaving out of snapshots what a player
can't see. Where the sources disagree with what Reluva has measured in
the game's own code, both are given.

Sources: Lilium Voyager (a GPL ioquake3 fork that plays Elite Force
Holomatch and talks to the retail game; its `ELITEFORCE` and
`LEGACY_PROTOCOL` branches in `qcommon/msg.c`, `net_chan.c`,
`qcommon.h`, `q_shared.h`, `server/sv_*.c`, `client/cl_*.c`) and plain
ioquake3 for comparison, read for the protocol's behaviour, not copied.
Names of fields and commands are given only so a reader can find their
counterparts there; the words that go on the wire (`getchallenge`, `cp`)
are part of the protocol.

## Protocol numbers

| Number | What it is |
|---|---|
| **24** | The retail game, patch 1.2 ("EF 1.2 protocol"). This is the one to implement. Lilium calls it the legacy protocol and turns on its `compat` code paths for it. |
| 25 | An older ioEF (iostvoyHM 1.37) native protocol. A Lilium server answers a version-25 connect with a message telling the client to fall back to 24. Not retail. |
| 26 | Lilium Voyager's and later ioEF's own protocol: ioquake3's protocol 71 machinery (Huffman, checksummed netchan, field-count deltas) carrying Elite Force's structures. Not understood by the retail game. |
| 1.0, 1.1 | Unknown. The retail executables before 1.2 aren't covered by these sources. |

Everything below is protocol 24 unless it says otherwise. Section
[Protocol 26](#protocol-26) summarises the other.

Retail data: `baseEF/pak0.pk3` (the CD) and `pak1.pk3` to `pak3.pk3`
(patches 1.1 and 1.2) must all be present for Lilium to run as Elite
Force. Lilium knows the four by their checksum (the pk3 "checksum" of
[pk3.md](pk3.md)'s pure scheme, without the feed): 3376297517, 596947475,
3960871590, 1592359207, in that order.

## Constants

| Name | Value | Notes |
|---|---|---|
| Server UDP port | 27960 | the LAN browser scans 27960–27963 |
| Master server port | 27953 | Quake III's is 27950 |
| Authorize server port | 27953 | Quake III's is 27952 |
| MOTD (update) server port | 27951 | |
| Largest datagram (`MAX_PACKETLEN`) | 1400 bytes | |
| Fragment size | 1300 bytes | |
| Largest message (`MAX_MSGLEN`) | 16384 bytes | after reassembly |
| `GENTITYNUM_BITS` | 10 | `MAX_GENTITIES` 1024; 1023 is "none" and ends entity lists, 1022 is the world |
| `MAX_CONFIGSTRINGS` | 1024 | 0 server info, 1 system info |
| `MAX_GAMESTATE_CHARS` | 16000 | |
| `MAX_RELIABLE_COMMANDS` | 64 | in each direction; indices are sequence & 63 |
| `PACKET_BACKUP` | 32 | snapshots kept for deltas |
| `MAX_PACKET_USERCMDS` | 32 | commands in one client packet |
| `MAX_SNAPSHOT_ENTITIES` | 256 | per snapshot |
| `MAX_CLIENTS` | 64 | `sv_maxclients` defaults to 8 |
| `MAX_STATS`, `MAX_PERSISTANT`, `MAX_WEAPONS`, `MAX_POWERUPS` | 16 each | |
| `MAX_PS_EVENTS` | 4 | Quake III has 2 |
| Area mask | up to 32 bytes | |
| `MAX_STRING_CHARS` / `MAX_INFO_STRING` / `BIG_INFO_STRING` | 1024 / 1024 / 8192 | |

The game state the client game reads (1024 offsets, 16,000 characters)
is Quake III's, which Reluva has confirmed independently
([eliteforce-syscalls.md](eliteforce-syscalls.md)).

## Transport

UDP over IPv4. A datagram whose first four bytes are `FF FF FF FF` is
**connectionless** (out of band): the rest is plain text, a command line
whose words are split as the console splits them (quoted strings keep
their spaces). Anything else is a **sequenced** packet of an
established connection (see [Netchan](#netchan)).

Out-of-band text is never compressed in Elite Force. In particular the
`connect` packet is plain text; Quake III 1.32 and ioquake3 Huffman-
compress everything after the first 12 bytes of `connect`, Elite Force
doesn't.

### Server discovery on a LAN

- The client broadcasts `getinfo xxx` (the `xxx` is an arbitrary
  challenge to be echoed) to 255.255.255.255 on ports 27960, 27961,
  27962 and 27963, each twice. (Lilium also multicasts to IPv6
  `ff04::696f:6566`; not retail.)
- The server answers `infoResponse` followed by a space and the info
  string **in double quotes**, with no newline:
  `FF FF FF FF infoResponse "\challenge\xxx\hostname\..."`. Quake III
  instead sends `infoResponse`, a newline, then the bare info string. A
  client should take the text between the first and second `"`.
- Keys Lilium's server puts in it: `challenge` (echoed), `gamename`
  (`EliteForce`; a retail server may leave it out, and Lilium's client
  accepts its absence), `protocol` (24), `hostname`, `mapname`,
  `clients` (connected, not counting private slots), `g_humanplayers`,
  `sv_maxclients` (minus private slots), `gametype`, `pure`,
  `g_needpass`, and when set `minPing`, `maxPing`, `game` (the mod
  folder). Which of these the retail menus require is unknown; `protocol`
  must be 24 or the retail browser is expected to drop the server (both
  Quake III and Lilium filter on it), and `hostname`, `mapname`,
  `clients`, `sv_maxclients` and `gametype` are what a browser shows.
- A client adds `nettype` to each entry for its menus. Elite Force's menus
  expect it as text (`udp`, `udp6`), where Quake III's expect a number.
- Single-player servers (`g_gametype` single player, or `ui_singlePlayerActive`)
  answer none of `getinfo`, `getstatus`, `getchallenge`.

`getstatus <challenge>` is answered with `statusResponse`, a newline, the
full server-info string (with `challenge` echoed), a newline, and then one
line per connected client: score, ping and quoted name, separated by
spaces. Same as Quake III. Raven's master queried servers with
`getstatus`, not `getinfo`, and listed only those whose status had
`protocol` 24, so a server should put `protocol` in its server info.

### Connecting

The client keeps a random 16-bit **qport**, chosen once at start-up, that
identifies its connection even if a NAT changes its source port.

1. **Challenge.** The client sends `getchallenge`; Lilium's client adds
   its own random number and `EliteForce` (`getchallenge 1234567
   EliteForce`), a retail client presumably neither (unknown). A server
   must accept both: Lilium's ignores a missing game name in protocol 24
   and rejects a different one with `print\nGame mismatch: This is a
   <game> server\n`. It retransmits every 3 seconds until answered.
2. The server picks a random 32-bit challenge for the address and
   answers `challengeResponse <challenge>`. Lilium's server adds the
   client's number and its protocol (`challengeResponse <challenge>
   <client's number> 26`); a client that gets only one number, as from a
   retail server, knows it is talking protocol 24.
3. **Connect.** The client sends `connect "<userinfo>"` (text, quoted),
   every 3 seconds until answered. The userinfo is the client's user
   settings with three keys added by the engine:
   - `protocol` — `24`. Any other number is refused with `print\nServer
     uses protocol version 24 (yours is N).\n`.
   - `qport` — the client's qport.
   - `challenge` — the number from `challengeResponse`.

   The server then reads `name`, `rate`, `snaps`, `handicap` and
   `password` (a `password` equal to `sv_privatePassword` may use the
   reserved `sv_privateClients` slots) and adds `ip`. The other keys are
   the game's (`model` such as `munro/red`, `color1`, `color2`, `sex`,
   `teamtask`, `cg_predictItems`...), passed to the server game.
4. The server checks the challenge for that address (clients on the
   loopback skip this), finds a slot (an existing one for the same
   address and qport or port is reused; reconnecting faster than
   `sv_reconnectlimit` seconds, 3, is ignored), asks the server game,
   and answers `connectResponse` (Lilium adds the challenge; the
   retail client needn't check it). A refusal is `print\n<reason>\n`
   (`Server is full.`, `No or bad challenge for your address.`, the
   game's reason, ban, ping limits).
5. The client sets up its netchan to the address that answered and
   starts sending sequenced packets at once; the server sends the
   game state when the first one arrives (see
   [Connection sequence](#connection-sequence)).

Other out-of-band commands: `print\n<text>` (server to client, shown to
the player), `disconnect` (ignored), `rcon <password> <command>`
(answered with `print` packets), `echo` (the client echoes its argument
back to its server).

### CD keys and authorization

A LAN game needs no CD key. Both sides skip authorization when the
other end's address is on the LAN (private ranges, loopback, the local
subnets) or IPv6. For an Internet address:

- The client, while asking for a challenge, sends the authorize server
  (`authenticate.stef1.ravensoft.com`, port 27953)
  `getKeyAuthorize <cl_anonymous> <key>` with the CD key's letters and
  digits only. Elite Force keeps the key in `efq3.key` (Quake III:
  `q3key`).
- The server, instead of answering `getchallenge` at once, sends the
  authorize server `getIpAuthorize <challenge> <a.b.c.d> ` (Elite Force
  sends nothing after the address; Quake III adds the game, a 0 and a
  strictness flag), and on `ipAuthorize <challenge> accept` sends the
  `challengeResponse`; `demo`, `unknown` or a refusal produce a `print`
  instead. If the authorize server can't be resolved, or hasn't answered
  for 5 seconds, the client is let in.

Raven's servers are long gone, so in practice every server lets everyone
in, but a retail server still tries them for a player it doesn't count as
local, and lets them in only once that has failed. Seen with a retail
server in a virtual machine joined from its host (October 2026): its
console showed `Resolving authenticate.stef1.ravensoft.com`, and the
challenge came after about 20 seconds (seven `getchallenge` requests, 3
seconds apart). The retail game would wait the same; nothing a client
can change.

## Netchan

A sequenced packet is a header then a message. All header numbers are
little-endian.

| Field | Size | Notes |
|---|---|---|
| sequence | 4 | outgoing sequence, starting at 1; bit 31 set on a fragment |
| qport | 2 | client to server only |
| fragment start | 2 | fragments only: offset of this piece in the message |
| fragment length | 2 | fragments only: bytes in this piece |
| message | rest | |

Protocol 24 has **no checksum word**. (ioquake3's protocol 71, and
Elite Force protocol 26, add 4 bytes after the qport: challenge XOR
(sequence × challenge), to defeat spoofing.)

**Fragments.** A message of 1300 bytes or more is sent in pieces of 1300
bytes, all with the same sequence number (bit 31 set). A piece shorter
than 1300 bytes is the last, so a message that is an exact multiple of
1300 bytes ends with an empty piece. A receiver collects the pieces of
one sequence in order; a piece whose start isn't the length collected
so far is dropped, which loses the message (what mattered in it is
resent at a higher level, as for any lost packet). When the last piece
arrives, the reassembled message is handled as if it had arrived
unfragmented under that sequence. The server
sends one piece per rate interval (see [Rate](#timing-and-rate)); the
client sends all its pieces at once. In protocol 24 Lilium does not
advance the channel's last-received sequence when a fragmented message
completes (as id's original code didn't; protocol 26 does).

**Order.** A packet whose sequence is not greater than the last one
accepted is dropped (duplicates and late packets alike). A gap is
counted as dropped packets and otherwise ignored: everything that must
arrive is resent at a higher level.

**Scrambling.** Every sequenced packet of protocol 24, header included,
is scrambled after it is built and unscrambled before it is read. The
first 6 bytes (the sequence and, from the client, the qport; from the
server, the sequence and the message's first two bytes) are left as
they are. Packets of 6 bytes or fewer aren't touched. With *n* the
packet's length:

1. seed = (the first four bytes read as a little-endian unsigned 32-bit
   number × 3) XOR (*n* × 123) XOR 0x87243987, in unsigned 32-bit
   arithmetic.
2. For *i* from 0 to *n* − 1: seed = 69069 × seed + 1 (mod 2^32), and
   r[*i*] = seed.
3. mask: the largest number of the form 2^*k* − 1 that is less than
   *n* − 6 (0 if *n* − 6 is 1 or less).
4. To scramble: XOR each byte *i* from 6 to *n* − 1 with the low byte of
   r[*i*]; then, for *i* from 6 up to *n* − 1, swap byte *i* with byte
   6 + (r[*i*] AND mask).
5. To unscramble: compute r and mask the same way (the first four bytes
   and the length are unchanged by scrambling); do the swaps in reverse,
   *i* from *n* − 1 down to 6; then XOR bytes 6 to *n* − 1 with the low
   bytes of r.

Quake III 1.32's protocol 68 doesn't scramble; it instead XORs the
message after its first 12 (client) or 4 (server) bytes with a key built
from the challenge, the sequence and the text of the last reliable
command. Elite Force has none of that, in either protocol.

## Bit stream

Protocol 24 messages are **not Huffman-compressed**. They are a plain
bit stream:

- Bits fill each byte from its least significant bit up.
- A value of *b* bits is written least significant bit first, so it
  occupies the remaining bits of the current byte, then the low bits of
  the following bytes. A 32-bit value written at a byte boundary is
  therefore its 4 little-endian bytes; one written at bit 3 straddles 5
  bytes.
- Byte, short and long are values of 8, 16 and 32 bits written this way:
  **not** aligned to bytes. A signed field (negative width in the tables
  below) is written as its low |*b*| bits and sign-extended on reading.
- A float is its 32 IEEE bits.
- **Strings and raw data are byte-aligned.** Before a string (or a
  big string, or a block of raw bytes such as the area mask or download
  data) the position moves to a byte boundary. Lilium does this, on
  both sides, by skipping to the start of the byte **after** the next
  boundary when the position isn't already on one: if the position is
  bit 3 of byte 10, the string starts at byte 12 (bits 3–7 of byte 10
  and all of byte 11 are unused). Whether the retail game skips that
  extra byte isn't independently confirmed; on a byte boundary nothing
  is skipped, which is the normal case (see the note after
  [Server messages](#server-to-client-messages)).
- A string is its bytes then a 0. A string is at most 1023 characters, a
  big string (configstrings) at most 8191. Elite Force passes bytes above
  127 through in both directions (Quake III replaces them with `.`); a
  reader replaces `%` with `.`.
- **End of message.** Protocol 24 sends no end marker. A reader asked for
  bits beyond the last byte gets "end of message" (Lilium returns −1 for
  a byte); the message parsers stop there. The bits after the last value
  in the last byte are zero.

Where Quake III writes an angle as a short, it's the angle × 65536 / 360,
masked to 16 bits.

### Deltas of plain values

A **delta field** is 1 bit: 0 means unchanged from the previous value; 1
is followed by the new value in the field's width. Elite Force uses no
keys (XOR masks) in any delta, in either protocol; Quake III 1.32 XORs
user command fields with a key.

### Small-integer floats

Float fields of entity and player states that hold a whole number from
−4096 to 4095 are sent short: 1 bit 0, then the number + 4096 in 13
bits. Any other value: 1 bit 1, then the 32 float bits.

## Entity state

The entity state is 204 bytes (51 words; Quake III's is 208, with
`generic1`). The number (offset 0) is never in the delta: it's written
before it. All fields are 32-bit words (ints or floats).

| # | Field | Offset | Bits |
|---|---|---|---|
| 0 | eType | 4 | 8 |
| 1 | eFlags | 8 | 24 |
| 2 | pos.trType | 12 | 8 |
| 3 | pos.trTime | 16 | 32 |
| 4 | pos.trDuration | 20 | 32 |
| 5–7 | pos.trBase x, y, z | 24, 28, 32 | float |
| 8–10 | pos.trDelta x, y, z | 36, 40, 44 | float |
| 11 | apos.trType | 48 | 8 |
| 12 | apos.trTime | 52 | 32 |
| 13 | apos.trDuration | 56 | 32 |
| 14–16 | apos.trBase x, y, z | 60, 64, 68 | float |
| 17–19 | apos.trDelta x, y, z | 72, 76, 80 | float |
| 20 | time | 84 | 32 |
| 21 | time2 | 88 | 32 |
| 22–24 | origin | 92, 96, 100 | float |
| 25–27 | origin2 | 104, 108, 112 | float |
| 28–30 | angles | 116, 120, 124 | float |
| 31–33 | angles2 | 128, 132, 136 | float |
| 34 | otherEntityNum | 140 | 10 |
| 35 | otherEntityNum2 | 144 | 10 |
| 36 | groundEntityNum | 148 | 10 |
| 37 | loopSound | 156 | 16 |
| 38 | constantLight | 152 | 32 |
| 39 | modelindex | 160 | 8 |
| 40 | modelindex2 | 164 | 8 |
| 41 | frame | 172 | 16 |
| 42 | clientNum | 168 | 8 |
| 43 | solid | 176 | 24 |
| 44 | event | 180 | 10 |
| 45 | eventParm | 184 | 8 |
| 46 | powerups | 188 | 16 |
| 47 | weapon | 192 | 8 |
| 48 | legsAnim | 196 | 8 |
| 49 | torsoAnim | 200 | 8 |

50 fields. The order is the struct's (Quake III's table is shuffled by
how often each field changes), except that loopSound comes before
constantLight and frame before clientNum. Compared with Quake III:
eFlags is 24 bits (19), loopSound 16 (8), and there is no generic1.

### Entity delta (protocol 24)

Written from an old state (the previous snapshot's, a baseline, or all
zeros) to a new one:

1. The entity number, 10 bits.
2. 1 bit: 1 means the entity is removed (nothing follows).
3. 1 bit: 0 means no field changed (nothing follows). A sender leaves out
   unchanged entities entirely unless it is forced to write them (new
   entities and baselines).
4. A **change vector**: 50 bits, bit *i* set if field *i* changed,
   stored as bytes (field *i* is bit *i* mod 8 of byte *i* / 8). It's
   sent as a 5-bit index into the table of predefined vectors below
   when it matches one exactly (the first match); otherwise the index is
   31 and the vector follows as 6 bytes (8 bits each, fields 0–47) and
   2 bits (fields 48, 49).
5. For each field whose bit is set, in order: a float as a small-integer
   float (above); an integer as its value in the field's width. There is
   no per-field "changed" bit and no "is zero" bit (Quake III has both).

Predefined change vectors (bytes 0–7, and the fields they mark):

| Index | Bytes | Fields |
|---|---|---|
| 0 | 60 80 00 00 00 00 00 00 | 5, 6, 15 |
| 1 | 60 c0 00 00 00 00 00 00 | 5, 6, 14, 15 |
| 2 | 60 00 00 00 00 00 00 00 | 5, 6 |
| 3 | 40 80 00 00 00 00 00 00 | 6, 15 |
| 4 | 20 80 00 00 00 00 00 00 | 5, 15 |
| 5 | 02 00 00 00 00 00 00 00 | 1 |
| 6 | 20 c0 00 00 00 00 00 00 | 5, 14, 15 |
| 7 | 40 c0 00 00 00 00 00 00 | 6, 14, 15 |
| 8 | 60 40 00 00 00 00 00 00 | 5, 6, 14 |
| 9 | 20 00 00 00 00 00 00 00 | 5 |
| 10 | 40 00 00 00 00 00 00 00 | 6 |
| 11 | 60 80 00 00 00 10 00 00 | 5, 6, 15, 44 |
| 12 | 60 80 00 00 01 00 00 00 | 5, 6, 15, 32 |
| 13 | 00 00 00 00 00 10 00 00 | 44 |
| 14 | 00 c0 00 00 00 00 00 00 | 14, 15 |
| 15 | e0 c0 00 00 00 00 00 00 | 5, 6, 7, 14, 15 |
| 16 | 60 c0 00 00 01 00 00 00 | 5, 6, 14, 15, 32 |
| 17 | 60 c0 00 00 00 10 00 00 | 5, 6, 14, 15, 44 |
| 18 | 00 80 00 00 00 00 00 00 | 15 |
| 19 | 60 c0 00 00 01 00 01 00 | 5, 6, 14, 15, 32, 48 |
| 20 | 60 c0 00 00 00 10 02 00 | 5, 6, 14, 15, 44, 49 |
| 21 | 60 80 00 00 01 00 01 00 | 5, 6, 15, 32, 48 |
| 22 | 60 00 00 00 00 10 00 00 | 5, 6, 44 |
| 23 | e1 00 c0 01 90 00 00 00 | 0, 5, 6, 7, 22, 23, 24, 36, 39 |
| 24 | ed 07 00 00 00 30 00 00 | 0, 2, 3, 5, 6, 7, 8, 9, 10, 44, 45 |
| 25 | 60 c0 00 00 00 00 02 00 | 5, 6, 14, 15, 49 |
| 26 | 62 c0 00 00 00 00 02 00 | 1, 5, 6, 14, 15, 49 |
| 27 | 02 00 00 00 00 10 00 00 | 1, 44 |
| 28 | e0 80 00 00 00 00 00 00 | 5, 6, 7, 15 |
| 29 | e0 00 00 00 00 00 00 00 | 5, 6, 7 |
| 30 | 60 80 00 00 00 00 02 00 | 5, 6, 15, 49 |
| 31 | — | explicit vector follows |

(They're the common cases of a moving player: position x/y (5, 6),
yaw (15), pitch (14), event (44), legs/torso animation (48, 49).) A
writer may always use index 31; a reader must know the table.

## Player state

Fields sent, in wire order:

| # | Field | Offset | Bits |
|---|---|---|---|
| 0 | commandTime | 0 | 32 |
| 1 | pm_type | 4 | 8 |
| 2 | bobCycle | 8 | 8 |
| 3 | pm_flags | 12 | 16 |
| 4 | pm_time | 16 | −16 |
| 5–7 | origin | 20, 24, 28 | float |
| 8–10 | velocity | 32, 36, 40 | float |
| 11 | weaponTime | 44 | −16 |
| 12 | gravity | 60 | 16 |
| 13 | speed | 64 | 16 |
| 14–16 | delta_angles | 68, 72, 76 | 16 |
| 17 | groundEntityNum | 80 | 10 |
| 18 | legsTimer | 84 | 8 |
| 19 | torsoTimer | 92 | 12 |
| 20 | legsAnim | 88 | 8 |
| 21 | torsoAnim | 96 | 8 |
| 22 | movementDir | 100 | 4 |
| 23 | eFlags | 104 | 16 |
| 24 | eventSequence | 108 | 16 |
| 25–28 | events[0–3] | 112, 116, 120, 124 | 8 |
| 29–32 | eventParms[0–3] | 128, 132, 136, 140 | 8 |
| 33 | externalEvent | 144 | 10 |
| 34 | externalEventParm | 148 | 8 |
| 35 | clientNum | 156 | 8 |
| 36 | weapon | 160 | 5 |
| 37 | weaponstate | 164 | 4 |
| 38–40 | viewangles | 168, 172, 176 | float |
| 41 | viewheight | 180 | −8 |
| 42 | damageEvent | 184 | 8 |
| 43 | damageYaw | 188 | 8 |
| 44 | damagePitch | 192 | 8 |
| 45 | damageCount | 196 | 8 |
| 46 | damageShieldCount | 200 | 8 |
| 47 | introTime | 56 | 32 |

48 fields. The offsets are Lilium's layout of the player state (see
[Layout cross-check](#layout-cross-check-with-reluva) for a conflict).
Not sent: the words at 48 (a phaser recharge timer) and 52 (a 16-bit
"use" debounce and 2 padding bytes), externalEventTime (152), and
everything after the arrays. The arrays: stats[16] at 204, persistant[16]
at 268, powerups[16] at 332, ammo[16] at 396; then ping (460) and an
entity event sequence (464), 468 bytes in all by Lilium's layout.

Compared with Quake III: four events and event parameters (two in
Quake III), damageShieldCount and introTime added; grapplePoint,
jumppad_ent, loopSound and generic1 gone; gravity, speed and the rest
are moved by the three Elite Force words at 48–56.

### Player state delta (protocol 24)

From the previous snapshot's player state, or all zeros when not
delta-compressing:

1. For every one of the 48 fields: 1 bit, changed or not; if changed,
   a float as a small-integer float, an integer in its width (signed
   ones sign-extended when read). Unlike the entity delta, every field
   has its bit; there is no field count (Quake III 1.32 sends a byte,
   the number of fields up to the last changed one, and bits only for
   those).
2. Four groups, in this order: **stats, persistant, ammo, powerups**
   (note: not the struct's order). For each: 1 bit, whether any element
   changed; if so, a 16-bit mask (bit *i* for element *i*), then each
   changed element in index order: stats, persistant and ammo as
   16-bit shorts (sign-extended), powerups as 32-bit longs. Protocol 24
   has no bit before the four groups saying whether any changed (Quake
   III and protocol 26 do).

## User commands

Elite Force's command is 24 bytes and laid out differently from Quake
III's: serverTime (int) at 0, buttons (a byte) at 4, weapon (a byte) at
5, angles[3] (ints, 16 significant bits) at 8, forwardmove, rightmove,
upmove (signed bytes) at 20, 21, 22. Buttons are 8 bits (Quake III: 16):
1 attack, 2 talk, 4 use holdable item, 8 gesture, 16 walking, 32 alt
attack, 64 use, 128 unused as far as seen.

A command is written as a delta from the one before it in the same
packet (the first from all zeros):

1. 1 bit: 1, then serverTime minus the previous one's in 8 bits
   (when less than 256); 0, then serverTime in 32 bits.
2. Delta fields (1 bit, then the value if it changed): angles[0],
   angles[1], angles[2] (16 bits each), forwardmove, rightmove, upmove
   (8), buttons (8), weapon (8).

No key, and no "anything changed" bit: Quake III 1.32 sends a bit saying
whether any field after the time changed, and XORs each changed field
with a key (the checksum feed, the acknowledged message and a hash of
the last server command, XOR the command's time). Elite Force does
neither, in both of its protocols. Readers turn a move of −128 into
−127 (a safeguard, not part of the format).

## Server to client messages

Opcodes (bytes): 0 bad, 1 nop, 2 gamestate, 3 configstring, 4 baseline,
5 serverCommand, 6 download, 7 snapshot, 8 EOF. These are Quake III's.
In protocol 24, 3 and 4 appear only inside a game state, and 8 is never
sent: a message ends where its data ends. (9 and 10 are ioquake3's VoIP,
not retail.)

A protocol 24 server message is a sequence of operations with nothing
before the first; in particular **no leading reliable acknowledge**
(Quake III 1.32 begins every server message with a long, the last
client command received; Elite Force puts it inside the snapshot
instead). A reader takes an opcode byte, handles it, and repeats until
the data runs out (or, for robustness, it reads 8).

**serverCommand (5)**: a long, the command's sequence number, then the
command as a string. Every message the server sends starts with all
commands the client hasn't acknowledged yet (sequence numbers from its
last acknowledgement + 1 to the newest). The client ignores numbers it
already has, stores the others at sequence & 63, and acknowledges the
highest it has in each packet it sends. A server more than 64 commands
ahead of a client's acknowledgement drops it.

**gamestate (2)**: the first message after connecting and after every
map change; sent alone (after any pending server commands).

1. A long: the server's current reliable command sequence (the client
   takes it as already received).
2. Any number of: byte 3, a short (configstring index), a big string;
   and byte 4, a baseline: an entity delta from all zeros (the entity
   number in 10 bits, then the not-removed bit 0, the changed bit 1, the
   change vector and the values). All configstrings come first, in index
   order, empty ones left out; then every baseline.
3. A byte 0 ends the list (Quake III and protocol 26 use 8, EOF). A
   reader should also stop at end of data.
4. A long: the **checksum feed** for the pure checks.

Protocol 24 does **not** send the client's own number here (Quake III
1.32 sends it as a long before the checksum feed). The engine passes 0
to the client game's init; the client game gets the player's number
from the snapshots' player states.

**snapshot (7)**:

| Field | Width |
|---|---|
| last client command received (reliable acknowledge) | 32 |
| server time (ms) | 32 |
| delta: how many messages back the old snapshot is, 0 for none | 8 |
| snap flags: 1 rate-delayed, 2 not active (connecting or zombie), 4 toggled at each map change | 8 |
| area mask length (bytes, at most 32) | 8 |
| area mask | that many bytes, byte-aligned |
| player state delta | |
| entities | |

The delta number is the server's outgoing sequence minus the message the
client last acknowledged with a delta-allowed move; the server sends 0
(a full snapshot) when the client asked for no delta, isn't active yet,
or the old snapshot is 29 or more messages old or its entities have been
overwritten. The client checks that it still has that snapshot (the
message numbered this message's sequence minus the delta); if not it
reads the snapshot and discards it, and asks for a full one.

The entities are a merge of the old and new snapshot's lists, each in
increasing entity number: an entity in both is written as a delta from
the old state (nothing if unchanged); one only in the new list, as a
forced delta from its baseline; one only in the old list, as its number
and the removed bit (1). The list ends with entity number 1023 in 10
bits.

While a protocol 24 client downloads, the server sends it a dummy
snapshot each time (so it gets the reliable acknowledge): no area mask
(length 0), an unchanged player state (48 zero bits and 4 zero bits),
and the end of the entity list straight away.

**download (6)**: a short, the block number; for block 0 a long, the
file's size (−1: refused; in protocol 24 no reason follows); then a
short, the block's length (up to 1024 bytes; 0 means the end of the
file), then that many bytes, byte-aligned. The client acknowledges each
with the command `nextdl <block>`, and asks with `download <name>`;
`stopdl` and `donedl` stop and finish. Up to 48 blocks are in flight.
The server sends no snapshots meanwhile. (After the dummy snapshot the
download's bytes start mid-byte, which is where the byte-alignment rule
above matters; Lilium's notes say it had to fix exactly this case for
protocol 24.)

**Text commands** the engine itself acts on, sent as server commands:
`cs <index> "<text>"` (a configstring changed); a configstring of 1000
characters or more goes in pieces, `bcs0 <index> "<first part>"`, `bcs1
<index> "<middle part>"`..., `bcs2 <index> "<last part>"`, which the
client joins and treats as one `cs`; `disconnect <reason>` (in protocol
24 the reason is unquoted, the rest of the line; protocol 26 quotes it);
`map_restart`; `print "<text>"`. Everything else is for the client game.

## Client to server messages

Opcodes: 0 bad, 1 nop, 2 move, 3 moveNoDelta, 4 clientCommand, 5 EOF.
Quake III's; 5 is never sent in protocol 24 (the message just ends).

| Field | Width |
|---|---|
| serverId | 32 |
| last server message received (its netchan sequence) | 32 |
| last server command received (its sequence) | 32 |
| any number of: byte 4, a long (command sequence), a string | |
| optionally: byte 2 or 3, a byte (command count, 1–32), the commands | |

- **serverId** comes from the system info configstring's `sv_serverid`.
  The server ignores a packet with a different one (after a map change)
  and, if the client isn't active and has acknowledged a message later
  than the game state, sends the game state again. Ids between the last
  map_restart's and the current one are ignored quietly.
- **Client commands** are sent until acknowledged (by the reliable
  acknowledge in snapshots): every unacknowledged one in every packet.
  The server ignores numbers it has executed, drops a client that skips
  one ("Lost reliable commands"), and executes at most one a second
  from an active client (flood protection, `sv_floodProtect`); others
  are passed to the server game later.
- **move (2)** lets the server delta the next snapshot from the one the
  client last received; **moveNoDelta (3)** asks for a full one. The
  client sends 3 when it has no valid snapshot, when the last message
  it received wasn't a snapshot it could use, or while recording a demo
  that's waiting for a full snapshot.
- The commands are the ones made since the packet 1 + `cl_packetdup`
  (default 1) packets ago, so each command goes out twice; the server
  runs only those newer than the last it ran.

Commands the engine handles (others go to the server game):
`userinfo "<info string>"` (sent whenever a user setting changes),
`disconnect`, `cp ...` (below), `vdr` (forget the client's pure check),
`download`, `nextdl`, `stopdl`, `donedl`.

## Connection sequence

| Client | Server |
|---|---|
| `getchallenge` (every 3 s) | `challengeResponse <n>` |
| `connect "<userinfo>"` (every 3 s) | slot taken, state **connected**; `connectResponse` |
| sequenced packets, one a second until it has a game state | on the first packet: game state, state **primed** |
| reads the game state, loads the map and the client game; sends `cp ...` then two packets at once | |
| moves (`clc_move`) | the first command while primed puts the player in the world (state **active**, the server game's ClientBegin); snapshots follow |

- A pure server (`sv_pure` 1) needs the `cp` command before the first
  move: if a move arrives from an active client that hasn't sent `cp`,
  the game state is sent again; a client whose `cp` fails is sent one
  snapshot and dropped ("Unpure client detected. Invalid .PK3 files
  referenced!").
- After a map change every connected client goes back to connected
  (bots straight to active), and the server sends a new game state when
  their next packet arrives. Until the client acknowledges it, its
  snapshots carry the server time as if the map hadn't changed.
- Timeouts: the server frees the slot of a client it hasn't heard from
  in `sv_timeout` seconds (200), without a zombie stage. A client that
  disconnects or is kicked is kept as a zombie for `sv_zombietime`
  (2 s), so its final reliable message can be repeated. The client gives
  up after `cl_timeout` (200 s).

### Timing and rate

- The server runs at `sv_fps` (20) frames a second. Elite Force puts
  `sv_fps` in the **system info** (Quake III doesn't), so clients see it.
- Snapshots: the userinfo's `snaps` (Lilium's default 20) is clamped to
  1..`sv_fps`; a client gets a snapshot every 1000 / snaps ms (50 ms
  without `snaps`).
- Rate: the userinfo's `rate` in bytes a second, clamped to 1000..90000,
  3000 if absent (`sv_minRate`, `sv_maxRate` narrow it). After sending a
  packet of *s* bytes (+ 28 for UDP/IP), the server waits *s* × 1000 /
  rate ms before sending that client another; a snapshot that has to
  wait is skipped and the next carries flag 1. Lilium doesn't rate-limit
  LAN clients.
- The client sends at most `cl_maxpackets` packets a second (Elite
  Force's default 43; Quake III's 30), clamped to 15..125.

### Pure servers

The system info carries `sv_paks` (the pure checksums of the server's
pk3s, as numbers separated by spaces), `sv_pakNames`, `sv_referencedPaks`
and `sv_referencedPakNames`, as in Quake III. The client restricts itself
to the listed pk3s and answers with `cp`. In protocol 24 the command is
`cp <cgame pk3> <ui pk3> @ <referenced pk3>... <count code>` — **without**
the server id Quake III 1.32 puts first. The numbers are pure checksums
(see [pk3.md](pk3.md): the MD4-based checksum of the checksum feed then
each non-empty file's CRC); the first is the pk3 holding `vm/cgame.qvm`,
the second the one holding `vm/ui.qvm`, then every other pk3 the client
has read from, then the checksum feed XOR all the referenced ones XOR
their count. The server checks: at least six words; the first two equal
its own cgame's and ui's pk3; then `@`; no repeats; each one is a pk3 it
has loaded; the last word matches.

A LAN server can avoid all this with `sv_pure 0` (empty `sv_paks` and
`sv_pakNames`); the client still sends `cp`, which is then ignored. A
pure server must hold the same 1.2 pk3s as the client.

## Configstrings and info keys the engine uses

- **0, server info**: every server-info setting; the engine itself adds
  `mapname`, `sv_hostname`, `sv_maxclients`, `sv_privateClients`,
  `sv_minRate`, `sv_maxRate`, `sv_minPing`, `sv_maxPing`,
  `sv_floodProtect`, `sv_dlRate`, `sv_allowDownload`, `sv_dlURL`,
  `sv_keywords`, `protocol`; the server game adds its own
  (`g_gametype`...).
- **1, system info**: `sv_serverid` (the server's frame time when the
  map loaded; it changes at every map load and restart), `sv_pure`,
  `sv_paks`, `sv_pakNames`, `sv_referencedPaks`, `sv_referencedPakNames`,
  `sv_cheats`, `sv_fps`, `fs_game` when a mod runs. The client sets each
  as a console variable.
- The rest are the games' own; Elite Force's numbering is Quake III's
  (models from 32, sounds from 288, players from 544,
  [eliteforce-syscalls.md](eliteforce-syscalls.md)).

The **checksum feed** is a random 32-bit number the server picks at each
map load; it seeds the pure checksums of both sides.

## Master server

For later. Elite Force's masters spoke Quake III's master protocol with
Raven's twists:

- Master: `master.stef1.ravensoft.com`, port 27953 (Raven's, gone;
  Lilium's second default is a community master, `efmaster.tjps.eu`).
  The client asks up to five masters (`sv_master1` to `sv_master5`),
  each separately.
- Heartbeat, from dedicated Internet servers (`dedicated 2`) every 300
  seconds, and when the server becomes empty, gets its first player or
  fills up: an out-of-band
  `\heartbeat\<server port>\gamename\STEF1\` (a GameSpy-style key list;
  Quake III sends `heartbeat QuakeArena-1` and a newline). The master then
  sends `getstatus` and needs `protocol` 24 in the answer. On shutdown
  the server sends two heartbeats so the master polls, gets no answer,
  and drops it.
- Server list query: `getservers <protocol> [keywords]` (no game name;
  ioquake3 puts one first for other games). The answer is
  `getserversResponse` followed by entries, each a `\` and then the IPv4
  address's 4 bytes and the port's 2 bytes (network order) **written as
  12 hexadecimal characters** (Quake III sends the 6 bytes raw). How the
  list ends (Quake III: `\EOT`) isn't shown by these sources.
- MOTD: `getmotd "<info>"` to `motd.stef1.ravensoft.com`, port 27951.

## Protocol 26

Lilium's own protocol, for reference: it is ioquake3's protocol 71 with
Elite Force's structures. Differences from protocol 24:

- Netchan: a checksum word after the qport (challenge XOR (sequence ×
  challenge)); no scrambling; a completed fragmented message advances
  the incoming sequence.
- Messages are Huffman-coded as in Quake III (adaptive Huffman trees
  seeded with the same 256-entry byte-frequency table as Quake III's
  `msg.c`, identical in Lilium and ioquake3; bits below a multiple of 8
  sent raw, whole bytes coded). Not needed for the retail game.
- Every server message starts with the reliable acknowledge long and
  ends with opcode 8; client messages end with 5; the game state's list
  ends with 8 and carries the client number before the checksum feed;
  the snapshot has no reliable acknowledge of its own.
- Entity deltas: a byte with the number of fields up to the last
  changed one, a bit per field up to it, and a bit for zero (float or
  integer) before each value; no predefined change vectors.
- Player state deltas: a field-count byte; one bit before the four
  arrays.
- `cp` starts with the server id; `disconnect` reasons are quoted; a
  refused download carries a reason string.
- User commands are the same as in 24 (no key).

## Layout cross-check with Reluva

Against [eliteforce-syscalls.md](eliteforce-syscalls.md) and
`src/engines/idtech3/eliteforce/game_layout.h`:

- **Entity state: agrees.** 204 bytes, no generic1 at the end (Reluva
  assumed this), modelindex at 160 and solid at 176 as Reluva has them;
  origin at 92.
- **User command: agrees** in every offset (serverTime 0, buttons 4,
  weapon 5, angles 8, moves 20–22, 24 bytes) and in the buttons being a
  byte.
- **Player state: conflicts.** Lilium's layout is 468 bytes with the
  timer at 48, a 16-bit field at 52, `introTime` at 56, gravity at 60,
  speed at 64, eventSequence at 108. Reluva measured the retail game
  code it runs at 460 bytes (copies of 460, the user command at client +
  464, snapshot entities at 504), with the timer at 48, 52 zero, and
  **gravity (800) at 56 and speed (250) at 60**. Reluva's player viewer
  reads the event sequence at 108, which agrees with Lilium but not with
  gravity at 56 unless something else moved. A likely explanation, not
  established: Reluva's QVMs are the CD's (`pak0.pk3`), and patch 1.2's
  game code (in `pak1`–`pak3`) added the holodeck-door intro timer at 56
  and one more word, giving Lilium's layout. The wire format above
  depends only on field order and widths, but a server must read the
  fields from the game's memory at the right offsets, and a retail 1.2
  client runs its own 1.2 client game; so a Reluva server for retail
  clients should run the 1.2 QVMs and take its offsets from them. Reluva's
  other observations fit Lilium's names with gravity at 60 except
  those: firing changes 44 (weaponTime), 160 (weapon in Lilium), 396
  (ammo[0] in Lilium); the use-item button changes 104 and 120.
- Configstrings and the game state: agree (1024 configstrings, 16,000
  characters, Quake III's numbering).

## Differences from Quake III (1.32, protocol 68)

- Protocol number 24; master and authorize port 27953; masters, MOTD and
  authorize hosts at `stef1.ravensoft.com`; heartbeat
  `\heartbeat\<port>\gamename\STEF1\`; `getservers` without a game name;
  server list entries in hexadecimal text.
- `infoResponse "<info>"` on one line with quotes; `nettype` as text.
- `connect` is not Huffman-compressed.
- No message compression at all: a raw least-significant-bit-first bit
  stream; strings and raw data aligned to bytes.
- No netchan XOR encoding; instead the whole datagram after byte 6 is
  scrambled (XOR with a linear congruential sequence, then a
  permutation) keyed by the sequence number and length.
- Server messages don't start with the reliable acknowledge; it's the
  first field of the snapshot. No end-of-message opcodes; the game
  state's list ends with 0 and has no client number.
- Entity deltas use a 50-bit change vector, often replaced by a 5-bit
  index into 31 predefined vectors, and carry no per-field or zero bits;
  player state deltas have a bit for every field and no count byte.
- Different entity and player state tables (struct order; EF fields;
  eFlags 24 bits, loopSound 16; four events).
- User commands: buttons are a byte; no key, no "anything changed" bit;
  different struct layout.
- `cp` has no server id; `disconnect <reason>` unquoted; a refused
  download has no message; a dummy snapshot precedes each download
  message.
- `sv_fps` is a system-info setting; `cl_maxpackets` defaults to 43; CD
  key in `efq3.key`; `getIpAuthorize` without the game and flags.
- Strings may carry bytes above 127.

## Open questions

- Which retail patches use which protocol numbers before 1.2 (1.0,
  1.1), and whether a 1.2 client can talk to them.
- Whether the retail game, like Lilium, skips a whole extra byte when
  aligning a string or data block that starts mid-byte (it matters only
  after the download's dummy snapshot).
- What a retail client sends with `getchallenge` (any arguments?) and
  whether it checks `connectResponse`'s argument.
- Which `infoResponse` keys the retail server browser needs, how many
  ports it scans, and whether it filters on `gamename`.
- How Raven's `getserversResponse` list ended.
- The player state's true layout in the 1.2 game code (see the
  cross-check): which pk3 holds the 1.2 QVMs, and the 8 bytes between
  Lilium's 468 and Reluva's measured 460.
- Whether any retail client or server sends opcode 1 (nop) or relies on
  the 0 that ends the game state for anything else.
- Retail defaults for `rate` and `snaps` (Lilium's are 25000 and 20).
