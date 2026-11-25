# Need for Speed II SE: network play on the host (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026: Phase 8 of [the plan](../PLAN.md),
two copies of the retail `nfs2sen.exe` on one machine, linked by each
of the game's three ways to play another machine, on the stand-ins NFS
III's network play was built on
([its plan](../../nfs3/PLAN.md), Network play).

## How it was run

Two `reluva-tool nfs host` runs on the real clock, each driven by a file
of keys its run reads as it goes (`-keysfile`), the keys written by
looking at each copy's latest frame:

```text
reluva-tool nfs host <install> -save <dir> -net -netlog <file> -frames <dir> -every 15
    -keysfile <keys.txt> [-netaddr 127.0.0.2 -broadcast 127.0.0.3]
    [-serial listen:5000 | -serial 127.0.0.1:5000]
    [-modem listen:5001 | -modem 127.0.0.1:5001]
```

A clock of instructions (`-deterministic`) didn't do: each copy's clock
then runs at its own speed, and the two drift apart by seconds while
one waits for the other. The game's network code times out by its own
clock, and copies joined in some runs and not others.

## What the game offers

*Game setup*'s **game type**: one player, split screen, modem, serial
link, network. A type other than one player or split screen makes the
main menu's RACE into CONNECT, which asks the player's name and then
the link's screen:

- **Network**: the protocol (IPX the only one offered), *create game*,
  *join game*. The program also has DirectPlay's names (`DirectPlay IPX`,
  `DirectPlay TCP`) and EA's TCP player code, but imports only
  `WSOCK32` and offered IPX alone here, with no DirectPlay installed.
- **Serial link**: the COM port (4 by default), *connect...*.
- **Modem**: the modem (TAPI's lines; here "Reluva TCP modem"), a phone
  list kept by the game, *delete number*, *dial...* (greyed until the
  list has a number), *answer...*. Up and Down don't move along this
  screen; the mouse does.

Then the same lobby for all three: the race's settings on the left, the
players in rows on the right (two rows for the serial link and modem,
eight for the network), each player's light red, then green once they've
pressed RACE. The host's RACE starts the race once everyone is ready;
both copies then show *synchronizing* and load the track.

## What was seen

| Link | Run | Lobby | Race |
|---|---|---|---|
| Network (IPX over UDP, `-netaddr` 127.0.0.2 and .3) | A creates, B lists A's game and joins | Both players in both lobbies; B's ready light green on A's | Proving Grounds, the two race clocks within 0.03 s; B driven, A standing; about 1,600 race packets each way a minute (43 and 16 bytes) |
| Serial link (one TCP link, COM4 on both) | Both *connect...* | Two rows, both players | The same, the clocks within 0.01 s |
| Modem (TAPI over TCP) | A answers; B adds 555 to its list and dials it | Two rows, both players | The same, the clocks within 0.03 s |

No stand-in needed changing: IPX's game finding (a broadcast to socket
`0x452`, `EAGAME.` names), its reliable layer (`ea` + `srv`, `dec`,
`add`, `rsp`, `dat`, `ola`, `out` messages), the serial line's `+++`/`ATE1`
exchange and TAPI's lines and calls all worked as NFS III's do. The
game's network library is EA's, as NFS III's (the same message names).

## Races to their end (later in November 2026)

Two copies on the serial link, each driven by the road driver
([road-driver.md](road-driver.md)), Proving Grounds, 4 laps, 8 cars:

| Run | Result |
|---|---|
| Both driven | The race to its end; the race summary the same on both: Player 1 4th, 4:07.53, Player 2 5th, 4:07.68 |
| B losing every 10th packet it sent (`-netdrop 10`) | The same: Player 2 2nd, 4:07.12, Player 1 3rd, 4:07.40, on both |
| B's process killed after A's second lap | A's race ended there: the race summary at once, both players *aborted* |
| Both the 3dfx program (`-program NFS2SEA.EXE`), Up held | Linked, the lobby, the race: the two race clocks within 0.06 s, each car seen in the other copy |

The other two links, the same way (late November 2026):

| Link | Result |
|---|---|
| Network (IPX), A creating, B joining | The race to its end, the summary and lap times the same on both: Player1 2nd, 4:06.90, Player2 5th, 4:07.28 |
| Modem, A answering, B dialling 555 | The same: Player2 2nd, 4:19.93, Player1 3rd, 4:20.06 |

**A tournament over IPX.** The race type is set in *game setup* before
connecting (in the lobby only the car, the gearbox and the colour can
be changed; the race's settings are the host's, greyed on the other).
After each race the summary, the lap times and the tournament's
standings, the same on both copies; *comms menu* takes both back to
the lobby, and RACE on both starts the next track. Each race's car
class is the tournament's (B's at Last Resort, Ford Indigos).

Four races of one run, both copies driven, every summary and standings
the same on both:

| Race | Track, cars | Player1 | Player2 ("2") |
|---|---|---|---|
| 1 | Proving Grounds, McLaren F1 | 6th, 4:22.68 | 5th, 4:22.59 |
| 2 | Outback, McLaren F1 | 7th, 9:30.71 | 8th, 9:30.93 |
| 3 | Last Resort, Ford Indigo | 7th, 10:09.59 | 8th, 10:11.71 |
| 4 | North Country, Mustang Mach III | 8th, 11:27.43 | 7th, 11:27.28 |

After the fourth, both 16 points, 7th and 8th. In the fifth the road
driver left Player1's car on a grass slope off the road, which it
couldn't climb back from ([road-driver.md](road-driver.md)); the link
held, the other copy racing on, and the run was stopped there. The
first runs found the road driver's other faults: on the Outback it
stopped for good against a sign and the rock pillars on the road's
middle, until it was mended.

**The whole tournament** (late November 2026), once the driver got free
of everything on every track: seven races, about 2 h 15 min on the
real clock, Proving Grounds, the Outback, Last Resort, North Country,
Pacific Spirit, Mediterraneo and Mystic Peaks, both copies driven to
every finish:

| Race | Player1 | Player2 ("2") |
|---|---|---|
| 1, Proving Grounds | 6th, 4:26.40 | 7th, 4:28.46 |
| 2 to 7 | 7th or 8th | 7th or 8th |

The tournament summary: Zipper champion with 195 points, Player2 7th
with 20, Player1 8th with 16. Each race's results, lap times and
standings were the same on both copies wherever both were seen (one
copy often a screen behind the other between races, the lobby putting
them in step again).

What was found on the way:

- **The controls go round the link, not the cars.** Each copy keeps a
  ring of every player's controls by frame (0x4fd5e8, 0x200 bytes a
  player), from which every car is driven (0x45e8a0, by 0x45e8d0). The
  local player's controls, sampled into a record (0x4ddb10), go into it
  and over the link each frame (0x448720; 0x45dae0 on a link); a frame
  whose remote controls haven't come takes the frame before's
  (0x45e780). The first driver, writing the controls after this, drove
  only its own copy's car: in the other, that car stood at the start,
  every copy waited for it to finish, and the race never ended (a
  still picture after the last car home). Written into the record
  before 0x448720, as the keys are, the controls reach both.
- **The lobby wasn't shown**: one thread polls the link, so no thread
  changes, and its drawing into the front surface never paused for the
  4 ms the host waited for before showing it. The host now shows it
  too before a native function, and after 25 ms at most of drawing.
- **Connecting, and a tie.** Clicked 15-25 s apart, the copies
  sometimes didn't reach the lobby: the one that waited went back to the
  link's screen, the other stayed on *connecting...*. A copy alone waits
  on *connecting...* for as long as it's left, Esc taking it back. The
  link's traffic (`-netlog`) shows the way: the `+++` and `ATE1` of the
  modem's commands, then each copy's name in a packet
  (`fe 14 01 ...`), in it at 10 a u16 that counts up at about 126 a
  second from the program's start. The copies' values differed by 1 to
  3 whenever they went on (`fe 5e 02 ...`, the lobby's settings, from
  the one that waited), and were equal each time they didn't: then the
  one that waited sent `fe 00 ff 01 ...` and gave up. Two copies started
  together on one machine count alike; two players' machines would
  rarely be within a count. Started 3 s apart, the values
  differed by about 0x380 and the late connects all went on. Not traced
  in the code which copy is chosen, or why a tie gives up
  *(inferred)*. The copies stopping to draw was the static
  *connecting...* box and lobby, not a hang: `threads` in the keys file
  (each thread's state, logged) showed the main thread at work and the
  link's packets going.
- **Keys** from `-keysfile` were lost in the menus on the real clock,
  4 of 64 a copy: the menus read their key buffer (0x475f20), act on a
  key, and read again, throwing away what came in between (0x405900);
  a key delivered while the main thread was between the two reads, at a
  function of another thread's (the sound's), was thrown away. The host
  now delivers input before the main thread's functions only (and at a
  change of threads once 20 ms late): 0 of 64 lost, on both copies.
  NFS II SE's game-files host records change with it (the keys land a
  little differently on the instruction clock; the run is the same).
- **The software and the 3dfx programs** linked (A `nfs2sen.exe`, driven;
  B `NFS2SEA.EXE`, Up held), clicked together: the lobby, and the race,
  the two clocks 0.1 s apart.

## Left open

- Where the game decides who hosts the link, and gives up on a tie.
- Copies on two machines; DirectPlay (not installed on the host, and
  the program offered no other protocol).
