# Need for Speed II SE: network play

> **Status:** done · **Code:** `src/engines/nfs/host/winsock.cpp`, `serial.cpp`, `src/engines/nfs/ui/play_page.cpp` · **Checked against:** two copies of the original on one machine, on Reluva's host · **Confidence:** from the code unless marked

## Overview

Two players race each other over a network (IPX), a serial link or a
modem. *Game setup*'s **game type** offers one player, split screen,
modem, serial link and network. A type other than one player or split
screen turns the main menu's RACE into CONNECT, which asks the player's
name and then shows the link's screen:

- **Network:** the protocol (IPX the only one offered), *create game*,
  *join game*.
- **Serial link:** the COM port (4 by default), *connect...*.
- **Modem:** the modem (TAPI's lines), a phone list kept by the game,
  *delete number*, *dial...* (greyed until the list has a number),
  *answer...*. Up and Down don't move along this screen; the mouse does.

Then the same lobby for all three: the race's settings on the left, the
players in rows on the right (two rows for the serial link and modem,
eight for the network), each player's light red, then green once
they've pressed RACE. The host's RACE starts the race once everyone is
ready; both copies show *synchronizing* and load the track.

The race type is set in *game setup* before connecting. In the lobby
only the car, the gearbox and the colour can be changed; the race's
settings are the host's, greyed on the other copy. In a tournament,
after each race come the summary, the lap times and the standings, the
same on both copies; *comms menu* takes both back to the lobby, and
RACE on both starts the next track, in the tournament's car class.

## How it works

Addresses are in `nfs2sen.exe`. Everything here was seen on two copies
on one machine
([research/network-play.md](../research/network-play.md)).

### The links

The game's network library is EA's, as NFS III's: the same message
names, and the same ways over each link
([NFS III's network messages](../../nfs3/systems/net.md)).

- **IPX:** a game is found by a broadcast to socket `0x452`, with
  `EAGAME.` names, then the reliable layer (`ea` with `srv`, `dec`,
  `add`, `rsp`, `dat`, `ola`, `out` messages).
- **Serial link:** the modem's commands first (`+++`, `ATE1`), then the
  reliable layer's frames (`fe` ...).
- **Modem:** TAPI's lines and calls.

The program also holds DirectPlay's names (`DirectPlay IPX`,
`DirectPlay TCP`) and EA's TCP player code, but imports only `WSOCK32`,
and offered IPX alone with no DirectPlay installed.

### Connecting

The connect message (0x45ca40, from the link's connecting at 0x45ce0b)
is a 20-byte payload, sent by 0x44fcb0:

| Bytes | What |
|---|---|
| 0 | The type, 6 |
| 1-3 | Not written: what was on the stack (`ff ff ff` seen) *(inferred)* |
| 4 | u32 the program's tick |
| 8 | The player's name (0x4de02c), NUL-ended; after a short name, what was on the stack *(inferred)* |

The name is copied without a limit: one over 11 characters would run
past the 20 bytes *(inferred)*.

The tick is at 0x51b024: zeroed as the program's timer starts
(0x477e1b), one more each time the timer thread wakes (0x4779cf), asked
for 128 times a second (0x47212c). Seen counting up at about 126 a
second from the program's start.

Received, by the type's handler (0x45ccf0, through the table at
0x4c4a7c), the message is kept at 0x4fd544 and a flag set (0x4de4b0),
which the sender waits on (0x45ca92).

### Who hosts a link

- **On the serial link** (0x4de4de = 2, 0x45cab8) the two ticks are
  compared, signed. The copy whose tick is greater hosts (0x4de4d8 = 1,
  the local player 0, 0x45cacf) and sends the lobby's settings
  (0x44f8a0, `fe 5e` ...). The lesser joins (0x4de4d8 = 0, the local
  player 1, 0x45cb48) and waits for them (0x45ce22).
- **A tie** of the ticks is decided by the names, as strings (0x4778f0):
  the greater hosts. Equal names too, connecting fails (0x45cb42).
- **On a modem** there's no comparison: the host is the copy already so
  marked, the answering one *(inferred)*.

So a tie needs both the tick and the name alike. Two copies started
together on one machine count alike, and the serial copies that tied
had both kept the default name, "Player1"; two players with their own
names don't tie.

### The tie, and the close frame

A copy whose connecting failed closes its connections (0x45ce98,
0x47f374). The packet layer's close (0x41d6b0) sends an empty frame of
sequence 0xff up to 4 times (0x41d710): `fe 00 ff 01`, the `01`
acknowledging the other's frame 1, its connect message. The other takes
that as the link closed (0x41e380). The `fe 00 fb` frames are
keep-alives (0x41d8c4).

Only one copy closes after a tie. The copy that waited gets the other's
connect message before it sends its own, so it compares at once, fails
and closes, its connect message and the close frames sent together. The
other, having sent its message, waits for the first's (0x45ca92) and
stops waiting only when that message's handler has set the flag, when
Esc is held (0x4c8585, scan code 1 in the keys' table), or, hosting on a
modem, at a time limit (0x44fb50); a closed link isn't among them. So it
stays on *connecting...* until Esc. That the first's connect message,
sent just before the close, never reaches its handler there is
*(inferred)*: had it, that copy would have compared and closed too.

A copy alone waits on *connecting...* for as long as it's left, Esc
taking it back.

### The controls go round the link

The controls go over the link, not the cars. Each copy keeps a ring of
every player's controls by frame (0x4fd5e8, 0x200 bytes a player), from
which every car is driven:

- The local player's controls are sampled from the keys into a record
  (0x4ddb10: s8 steering, negative left; u8 gas; u8 brake; u8 buttons,
  0x02 shift up and 0x01 down).
- Each frame they go into the ring and over the link (0x448720; 0x45dae0
  on a link).
- Each car's input function (`car+0x370`, 0x45b3a0) takes its player's
  back from the ring (0x45e8a0, by 0x45e8d0) into 0x4ddb18, and 0x445440
  into the car.
- A frame whose remote controls haven't come takes the frame before's
  (0x45e780).

So both copies run every car alike. Controls written into the car after
the ring reach one copy only: in the other that car stands at the
start, and the race never ends, every copy waiting for it to finish (seen
with the road driver's first version,
[research/road-driver.md](../research/road-driver.md)).

### Races

On each link the two race clocks were within a few hundredths of a
second (0.03 s over IPX and the modem, 0.01 s on the serial link), and
over IPX about 1,600 race packets went each way a minute (43 and 16
bytes). Races to their end gave the same summary and lap times on both
copies:

- on all three links, both cars driven, 4 laps of Proving Grounds;
- on the serial link with every 10th packet of one copy lost;
- over IPX, a whole tournament of seven races, its results, lap times
  and standings the same on both copies wherever both were seen.

A copy leaving mid-race ends the race for the other: the race summary at
once, both players *aborted*. The software program and the 3dfx program
link and race each other.

## In Reluva

The game's own code makes the links; Reluva gives it the machine's
network underneath, with the stand-ins NFS III's network play runs on,
unchanged:

- `src/engines/nfs/host/winsock.cpp`: the game's Winsock, IPX carried
  over UDP. `broadcast=` (addresses or host names, by commas) sends the
  game's searches to those machines instead of the local network
  (nfs host `-broadcast`). The game addresses its packets by each
  machine's own address, so across the internet the two need to share a
  network (a VPN).
- `src/engines/nfs/host/serial.cpp`: its COM ports and its modem, each
  link one TCP connection (`serial=`, `modem=`; nfs host `-serial`,
  `-modem`): `listen:<port>` on one copy, `<host>:<port>` on the other.
- `network=off` leaves the network out.

How to set them up is in [the game's README](../README.md#using-it).
These are listed in [KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md).

## Sources and checks

- **Where the facts come from:** what the game offers and does, seen
  on two copies of `nfs2sen.exe` on one machine, each run by
  `reluva-tool nfs host` on the real clock and driven by keys from a file
  (`-keysfile`) or by the road driver (`-roaddrive`); the link's traffic
  logged (`-netlog`); connecting, who hosts and the controls' ring read
  in the program's code. The working notes they came from are
  [research/network-play.md](../research/network-play.md) and
  [research/road-driver.md](../research/road-driver.md).
- **How it was checked:** races on each link run to their end, the
  results compared on both copies; packets dropped on purpose
  (`-netdrop`); a copy killed mid-race; late connects on the serial link
  ([TESTING.md](../TESTING.md#last-checked)).

## Open questions

- Copies on two machines haven't been tried.
- DirectPlay: not installed on the host, and the program offered no
  other protocol than IPX.
- Why the waiting copy never handles the connect message sent before the
  close ([The tie, and the close frame](#the-tie-and-the-close-frame)).
